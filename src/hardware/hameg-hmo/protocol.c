/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
 * Copyright (C) 2018 Guido Trentalancia <guido@trentalancia.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <math.h>
#include <stdlib.h>
#include "scpi.h"
#include "protocol.h"

SR_PRIV void hmo_queue_logic_data(struct dev_context *devc,
				  const size_t group, const GByteArray *pod_data);
SR_PRIV void hmo_send_logic_packet(const struct sr_dev_inst *sdi,
				   const struct dev_context *devc);
SR_PRIV void hmo_cleanup_logic_data(struct dev_context *devc);

/*
 * This is the basic dialect supported on the Hameg HMO series
 * and on the Rohde&Schwarz HMO and RTC1000 series.
 *
 * It doesn't support directly setting the sample rate, although
 * it supports setting the maximum sample rate.
 */
static const char *hameg_scpi_dialect[] = {
	[SCPI_CMD_GET_DIG_DATA]		      = ":FORM UINT,8;:POD%d:DATA?",
	[SCPI_CMD_GET_TIMEBASE]		      = ":TIM:SCAL?",
	[SCPI_CMD_SET_TIMEBASE]		      = ":TIM:SCAL %s",
	[SCPI_CMD_GET_HORIZONTAL_DIV]	      = ":TIM:DIV?",
	[SCPI_CMD_GET_COUPLING]		      = ":CHAN%d:COUP?",
	[SCPI_CMD_SET_COUPLING]		      = ":CHAN%d:COUP %s",
	[SCPI_CMD_GET_SAMPLE_RATE]	      = ":ACQ:SRAT?",
	[SCPI_CMD_GET_WAVEFORM_SAMPLE_RATE]   = ":ACQ:WRAT?",
	[SCPI_CMD_SET_WAVEFORM_SAMPLE_RATE]   = ":ACQ:WRAT %s",
	[SCPI_CMD_GET_INTERPOLATION_MODE]     = ":ACQ:INT?",
	[SCPI_CMD_SET_INTERPOLATION_MODE]     = ":ACQ:INT %s",
	[SCPI_CMD_GET_ANALOG_DATA]	      = ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CHAN%d:DATA?",
	[SCPI_CMD_GET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL?",
	[SCPI_CMD_SET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL %s",
	[SCPI_CMD_GET_DIG_POD_STATE]	      = ":POD%d:STAT?",
	[SCPI_CMD_SET_DIG_POD_STATE]	      = ":POD%d:STAT %d",
	[SCPI_CMD_GET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR?",
	[SCPI_CMD_SET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR %s",
	[SCPI_CMD_GET_TRIGGER_SLOPE]	      = ":TRIG:A:EDGE:SLOP?",
	[SCPI_CMD_SET_TRIGGER_SLOPE]	      = ":TRIG:A:TYPE EDGE;:TRIG:A:EDGE:SLOP %s",
	[SCPI_CMD_GET_TRIGGER_PATTERN]	      = ":TRIG:A:PATT:SOUR?",
	[SCPI_CMD_SET_TRIGGER_PATTERN]	      = ":TRIG:A:TYPE LOGIC;" \
					        ":TRIG:A:PATT:FUNC AND;" \
					        ":TRIG:A:PATT:COND \"TRUE\";" \
					        ":TRIG:A:PATT:MODE OFF;" \
					        ":TRIG:A:PATT:SOUR \"%s\"",
	[SCPI_CMD_GET_HIGH_RESOLUTION]	      = ":ACQ:HRES?",
	[SCPI_CMD_SET_HIGH_RESOLUTION]	      = ":ACQ:HRES %s",
	[SCPI_CMD_GET_PEAK_DETECTION]	      = ":ACQ:PEAK?",
	[SCPI_CMD_SET_PEAK_DETECTION]	      = ":ACQ:PEAK %s",
	[SCPI_CMD_GET_DIG_CHAN_STATE]	      = ":LOG%d:STAT?",
	[SCPI_CMD_SET_DIG_CHAN_STATE]	      = ":LOG%d:STAT %d",
	[SCPI_CMD_GET_VERTICAL_OFFSET]	      = ":CHAN%d:POS?",
	[SCPI_CMD_GET_HORIZ_TRIGGERPOS]	      = ":TIM:POS?",
	[SCPI_CMD_SET_HORIZ_TRIGGERPOS]	      = ":TIM:POS %s",
	[SCPI_CMD_GET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT?",
	[SCPI_CMD_SET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT %d",
	[SCPI_CMD_GET_PROBE_UNIT]	      = ":PROB%d:SET:ATT:UNIT?",
	[SCPI_CMD_GET_DIG_POD_THRESHOLD]      = ":POD%d:THR?",
	[SCPI_CMD_SET_DIG_POD_THRESHOLD]      = ":POD%d:THR %s",
	[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD] = ":POD%d:THR:UDL%d?",
	[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD] = ":POD%d:THR:UDL%d %s",
	[SCPI_CMD_GET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR?",
	[SCPI_CMD_SET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR:DEF \"%s\"",
	[SCPI_CMD_GET_FFT_SAMPLE_RATE]	      = ":CALC:MATH%d:FFT:SRAT?",
	[SCPI_CMD_SET_FFT_SAMPLE_RATE]	      = ":CALC:MATH%d:FFT:SRAT %s",
	[SCPI_CMD_GET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE?",
	[SCPI_CMD_SET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP?",
	[SCPI_CMD_SET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN?",
	[SCPI_CMD_SET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR %s",
	[SCPI_CMD_GET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:ADJ?",
	[SCPI_CMD_SET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:VAL %s",
	[SCPI_CMD_GET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO %s",
	[SCPI_CMD_GET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT %d",
	[SCPI_CMD_GET_FFT_DATA]		      = ":CALC:MATH%d:ARIT OFF;" \
					        ":CALC:MATH%d:FFT:MAGN:SCAL DBM;" \
					        ":CALC:MATH%d:SCAL 20;" \
					        ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CALC:MATH%d:DATA?",
};

/*
 * This dialect is used by the Rohde&Schwarz RTB2000, RTM3000 and
 * RTA4000 series.
 *
 * It doesn't support setting the sample rate.
 */
static const char *rohde_schwarz_log_not_pod_scpi_dialect[] = {
	[SCPI_CMD_GET_DIG_DATA]		      = ":FORM UINT,8;:LOG%d:DATA?",
	[SCPI_CMD_GET_TIMEBASE]		      = ":TIM:SCAL?",
	[SCPI_CMD_SET_TIMEBASE]		      = ":TIM:SCAL %s",
	[SCPI_CMD_GET_HORIZONTAL_DIV]	      = ":TIM:DIV?",
	[SCPI_CMD_GET_COUPLING]		      = ":CHAN%d:COUP?",
	[SCPI_CMD_SET_COUPLING]		      = ":CHAN%d:COUP %s",
	[SCPI_CMD_GET_SAMPLE_RATE]	      = ":ACQ:SRAT?",
	[SCPI_CMD_GET_WAVEFORM_SAMPLE_RATE]   = ":ACQ:WRAT?",
	[SCPI_CMD_SET_WAVEFORM_SAMPLE_RATE]   = ":ACQ:WRAT %s",
	[SCPI_CMD_GET_INTERPOLATION_MODE]     = ":ACQ:INT?",
	[SCPI_CMD_SET_INTERPOLATION_MODE]     = ":ACQ:INT %s",
	[SCPI_CMD_GET_ANALOG_DATA]	      = ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CHAN%d:DATA?",
	[SCPI_CMD_GET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL?",
	[SCPI_CMD_SET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL %s",
	[SCPI_CMD_GET_DIG_POD_STATE]	      = ":LOG%d:STAT?",
	[SCPI_CMD_SET_DIG_POD_STATE]	      = ":LOG%d:STAT %d",
	[SCPI_CMD_GET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR?",
	[SCPI_CMD_SET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR %s",
	[SCPI_CMD_GET_TRIGGER_SLOPE]	      = ":TRIG:A:EDGE:SLOP?",
	[SCPI_CMD_SET_TRIGGER_SLOPE]	      = ":TRIG:A:TYPE EDGE;:TRIG:A:EDGE:SLOP %s",
	[SCPI_CMD_GET_TRIGGER_PATTERN]	      = ":TRIG:A:PATT:SOUR?",
	[SCPI_CMD_SET_TRIGGER_PATTERN]	      = ":TRIG:A:TYPE LOGIC;" \
					        ":TRIG:A:PATT:FUNC AND;" \
					        ":TRIG:A:PATT:COND \"TRUE\";" \
					        ":TRIG:A:PATT:MODE OFF;" \
					        ":TRIG:A:PATT:SOUR \"%s\"",
	[SCPI_CMD_GET_HIGH_RESOLUTION]	      = ":ACQ:HRES?",
	[SCPI_CMD_SET_HIGH_RESOLUTION]	      = ":ACQ:HRES %s",
	[SCPI_CMD_GET_PEAK_DETECTION]	      = ":ACQ:PEAK?",
	[SCPI_CMD_SET_PEAK_DETECTION]	      = ":ACQ:PEAK %s",
	[SCPI_CMD_GET_DIG_CHAN_STATE]	      = ":LOG%d:STAT?",
	[SCPI_CMD_SET_DIG_CHAN_STATE]	      = ":LOG%d:STAT %d",
	[SCPI_CMD_GET_VERTICAL_OFFSET]	      = ":CHAN%d:POS?",	/* Might not be supported on RTB200x... */
	[SCPI_CMD_GET_HORIZ_TRIGGERPOS]	      = ":TIM:POS?",
	[SCPI_CMD_SET_HORIZ_TRIGGERPOS]	      = ":TIM:POS %s",
	[SCPI_CMD_GET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT?",
	[SCPI_CMD_SET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT %d",
	[SCPI_CMD_GET_PROBE_UNIT]	      = ":PROB%d:SET:ATT:UNIT?",
	[SCPI_CMD_GET_DIG_POD_THRESHOLD]      = ":DIG%d:TECH?",
	[SCPI_CMD_SET_DIG_POD_THRESHOLD]      = ":DIG%d:TECH %s",
	[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD] = ":DIG%d:THR?",
	[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD] = ":DIG%d:THR %s",
	[SCPI_CMD_GET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR?",
	[SCPI_CMD_SET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR:DEF \"%s\"",
	[SCPI_CMD_GET_FFT_SAMPLE_RATE]	      = ":CALC:MATH%d:FFT:SRAT?",
	[SCPI_CMD_SET_FFT_SAMPLE_RATE]	      = ":CALC:MATH%d:FFT:SRAT %s",
	[SCPI_CMD_GET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE?",
	[SCPI_CMD_SET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP?",
	[SCPI_CMD_SET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN?",
	[SCPI_CMD_SET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR %s",
	[SCPI_CMD_GET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:ADJ?",
	[SCPI_CMD_SET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:VAL %s",
	[SCPI_CMD_GET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO %s",
	[SCPI_CMD_GET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT %d",
	[SCPI_CMD_GET_FFT_DATA]		      = ":CALC:MATH%d:ARIT OFF;" \
					        ":CALC:MATH%d:FFT:MAGN:SCAL DBM;" \
					        ":CALC:MATH%d:SCAL 20;" \
					        ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CALC:MATH%d:DATA?",
};

/*
 * This dialect is used by the Rohde&Schwarz RTO2000 series.
 *
 * It support directly setting the sample rate to any desired
 * value up to the maximum allowed.
 *
 * It doesn't provide a separate setting for the FFT sample rate
 * as in the HMO, RTC1000, RTB2000, RTM3000 and RTA4000 series.
 *
 * At the moment the High Resolution and Peak Detection modes
 * are not implemented.
 */
static const char *rohde_schwarz_rto200x_scpi_dialect[] = {
	[SCPI_CMD_GET_DIG_DATA]		      = ":LOG%d:DATA?",
	[SCPI_CMD_GET_TIMEBASE]		      = ":TIM:SCAL?",
	[SCPI_CMD_SET_TIMEBASE]		      = ":TIM:SCAL %s",
	[SCPI_CMD_GET_HORIZONTAL_DIV]	      = ":TIM:DIV?",
	[SCPI_CMD_GET_COUPLING]		      = ":CHAN%d:COUP?",
	[SCPI_CMD_SET_COUPLING]		      = ":CHAN%d:COUP %s",
	[SCPI_CMD_GET_SAMPLE_RATE]	      = ":ACQ:SRAT?",
	[SCPI_CMD_SET_SAMPLE_RATE]	      = ":ACQ:SRAT %s",
	[SCPI_CMD_GET_INTERPOLATION_MODE]     = ":ACQ:INT?",
	[SCPI_CMD_SET_INTERPOLATION_MODE]     = ":ACQ:INT %s",
	[SCPI_CMD_GET_ANALOG_DATA]	      = ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CHAN%d:DATA?",
	[SCPI_CMD_GET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL?",
	[SCPI_CMD_SET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL %s",
	[SCPI_CMD_GET_DIG_POD_STATE]	      = ":BUS%d:PAR:STAT?",
	[SCPI_CMD_SET_DIG_POD_STATE]	      = ":BUS%d:PAR:BIT%d:STAT %d",
	[SCPI_CMD_GET_TRIGGER_SOURCE]	      = ":TRIG1:SOUR?",
	[SCPI_CMD_SET_TRIGGER_SOURCE]	      = ":TRIG1:SOUR %s",
	[SCPI_CMD_GET_TRIGGER_SLOPE]	      = ":TRIG1:EDGE:SLOP?",
	[SCPI_CMD_SET_TRIGGER_SLOPE]	      = ":TRIG1:TYPE EDGE;:TRIG1:EDGE:SLOP %s",
	[SCPI_CMD_GET_TRIGGER_PATTERN]	      = ":TRIG1:PAR:PATT:BIT%d?",
	[SCPI_CMD_SET_TRIGGER_PATTERN]	      = ":TRIG1:PAR:TYPE PATT;" \
					        ":TRIG1:PAR:PATT:MODE OFF;" \
					        ":TRIG1:PAR:PATT:BIT%d %s",
/* TODO: High Resolution and Peak Detection modes are based on channel and waveform number. */
	[SCPI_CMD_GET_DIG_CHAN_STATE]	      = ":BUS%d:PAR:BIT%d:STAT?",
	[SCPI_CMD_SET_DIG_CHAN_STATE]	      = ":BUS%d:PAR:BIT%d:STAT %d",
	[SCPI_CMD_GET_VERTICAL_OFFSET]	      = ":CHAN%d:POS?",
	[SCPI_CMD_GET_HORIZ_TRIGGERPOS]	      = ":TIM:HOR:POS?",
	[SCPI_CMD_SET_HORIZ_TRIGGERPOS]	      = ":TIM:HOR:POS %s",
	[SCPI_CMD_GET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT?",
	[SCPI_CMD_SET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT %d",
	[SCPI_CMD_GET_PROBE_UNIT]	      = ":PROB%d:SET:ATT:UNIT?",
	[SCPI_CMD_GET_DIG_POD_THRESHOLD]      = ":BUS%d:PAR:TECH?",
	[SCPI_CMD_SET_DIG_POD_THRESHOLD]      = ":BUS%d:PAR:TECH %s",
	[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD] = ":BUS%d:PAR:THR%d?",
	[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD] = ":BUS%d:PAR:THR%d %s",
	[SCPI_CMD_GET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR?",
	[SCPI_CMD_SET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR:DEF \"%s\"",
/*	[SCPI_CMD_GET_FFT_SAMPLE_RATE] missing, as of User Manual version 12 ! */
/*	[SCPI_CMD_SET_FFT_SAMPLE_RATE] missing, as of User Manual version 12 ! */
	[SCPI_CMD_GET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE?",
	[SCPI_CMD_SET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP?",
	[SCPI_CMD_SET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN?",
	[SCPI_CMD_SET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR %s",
	[SCPI_CMD_GET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:ADJ?",
	[SCPI_CMD_SET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:VAL %s",
	[SCPI_CMD_GET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO %s",
	[SCPI_CMD_GET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT %d",
	[SCPI_CMD_GET_FFT_DATA]		      = ":CALC:MATH%d:ARIT OFF;" \
					        ":CALC:MATH%d:FFT:MAGN:SCAL DBM;" \
					        ":CALC:MATH%d:VERT:SCAL 20;" \
					        ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CALC:MATH%d:DATA?",
};

static const uint32_t devopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_WAVEFORM_SAMPLE_RATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_INTERPOLATION_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_PATTERN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_HIGH_RESOLUTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PEAK_DETECTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_WINDOW | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_FFT_FREQUENCY_START | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_STOP | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_SPAN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_CENTER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_RESOLUTION_BW | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_SPAN_RBW_COUPLING | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_SPAN_RBW_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_analog[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VSCALE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_digital[] = {
	SR_CONF_LOGIC_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LOGIC_THRESHOLD_CUSTOM | SR_CONF_GET | SR_CONF_SET,
};

/*
 * Waveform acquisition rate / sample rate option arrays for
 * different oscilloscope models.
 *
 * IMPORTANT: Always place the Maximum Sample Rate option
 *            (usually named "MSAM") at index position
 *            MAXIMUM_SAMPLE_RATE_INDEX (see protocol.h) !
 */

/* Segmented memory option available (manual setting). */
static const char *waveform_sample_rate[] = {
	"AUTO",
	"MWAV",
	"MSAM",
	"MAN",
};

/* RTC1000, HMO1002/1202 and HMO Compact series have no
 * segmented memory option available (no manual setting).
 */
static const char *waveform_sample_rate_nosegmem[] = {
	"AUTO",
	"MWAV",
	"MSAM",
};

static const char *interpolation_mode[] = {
	"LIN",
	"SINX",
	"SMHD",
};

static const char *coupling_options[] = {
	"AC",  // AC with 50 Ohm termination (152x, 202x, 30xx, 1202)
	"ACL", // AC with 1 MOhm termination
	"DC",  // DC with 50 Ohm termination
	"DCL", // DC with 1 MOhm termination
	"GND",
};

static const char *coupling_options_rtb200x[] = {
	"ACL", // AC with 1 MOhm termination
	"DCL", // DC with 1 MOhm termination
	"GND",
};

static const char *coupling_options_rtm300x[] = {
	"ACL", // AC with 1 MOhm termination
	"DC",  // DC with 50 Ohm termination
	"DCL", // DC with 1 MOhm termination
	"GND",
};

static const char *coupling_options_rto200x[] = {
	"AC",  // AC with 1 MOhm termination
	"DC",  // DC with 50 Ohm termination
	"DCL", // DC with 1 MOhm termination
	"GND", // Mentioned in datasheet version 03.00, but not in User Manual version 12 !
};

static const char *scope_trigger_slopes[] = {
	"POS",
	"NEG",
	"EITH",
};

/* Predefined logic thresholds. */
static const char *logic_threshold[] = {
	"TTL",
	"ECL",
	"CMOS",
	"USER1",
	"USER2", // overwritten by logic_threshold_custom, use USER1 for permanent setting
};

static const char *logic_threshold_rtb200x_rtm300x[] = {
	"TTL",
	"ECL",
	"CMOS",
	"MAN", // overwritten by logic_threshold_custom
};

static const char *logic_threshold_rto200x[] = {
	"V15",  // TTL
	"V25",  // CMOS 5V
	"V165", // CMOS 3.3V
	"V125", // CMOS 2.5V
	"V09",  // CMOS 1.85V
	"VM13", // ECL -1.3V
	"V38",  // PECL
	"V20",  // LVPECL
	"V0",   // Ground
	"MAN",  // overwritten by logic_threshold_custom
};

/* This might need updates whenever logic_threshold* above change. */
#define MAX_NUM_LOGIC_THRESHOLD_ENTRIES ARRAY_SIZE(logic_threshold_rto200x)

/* FFT window types available on the HMO series */
static const char *fft_window_types_hmo[] = {
	"RECT",
	"HAMM",
	"HANN",
	"BLAC",
};

/* FFT window types available on the RT series, except RTO200x */
static const char *fft_window_types_rt[] = {
	"RECT",
	"HAMM",
	"HANN",
	"BLAC",
	"FLAT",
};

/* FFT window types available on the RTO200x */
static const char *fft_window_types_rto200x[] = {
	"RECT",
	"HAMM",
	"HANN",
	"BLAC",
	"GAUS",
	"FLAT",
	"KAIS",
};

/* RTC1002, HMO Compact2 and HMO1002/HMO1202 */
static const char *an2_dig8_trigger_sources[] = {
	"CH1", "CH2",
	"LINE", "EXT", "PATT", "BUS1", "BUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
};

/* HMO3xx2 */
static const char *an2_dig16_trigger_sources[] = {
	"CH1", "CH2",
	"LINE", "EXT", "PATT", "BUS1", "BUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

/* RTB2002 and RTM3002 */
static const char *an2_dig16_sbus_trigger_sources[] = {
	"CH1", "CH2",
	"LINE", "EXT", "PATT", "SBUS1", "SBUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

/* HMO Compact4 */
static const char *an4_dig8_trigger_sources[] = {
	"CH1", "CH2", "CH3", "CH4",
	"LINE", "EXT", "PATT", "BUS1", "BUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
};

/* HMO3xx4 and HMO2524 */
static const char *an4_dig16_trigger_sources[] = {
	"CH1", "CH2", "CH3", "CH4",
	"LINE", "EXT", "PATT", "BUS1", "BUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

/* RTB2004, RTM3004 and RTA4004 */
static const char *an4_dig16_sbus_trigger_sources[] = {
	"CH1", "CH2", "CH3", "CH4",
	"LINE", "EXT", "PATT", "SBUS1", "SBUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

/* RTO200x */
static const char *rto200x_trigger_sources[] = {
	"CHAN1", "CHAN2", "CHAN3", "CHAN4",
	"MSOB1", "MSOB2", "MSOB3", "MSOB4",
	"EXT", "LOGI", "SBUS",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

static const uint64_t timebases[][2] = {
	/* nanoseconds */
	{ 1, 1000000000 },
	{ 2, 1000000000 },
	{ 5, 1000000000 },
	{ 10, 1000000000 },
	{ 20, 1000000000 },
	{ 50, 1000000000 },
	{ 100, 1000000000 },
	{ 200, 1000000000 },
	{ 500, 1000000000 },
	/* microseconds */
	{ 1, 1000000 },
	{ 2, 1000000 },
	{ 5, 1000000 },
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 50, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 500, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
};

/* HMO Compact series (HMO722/724/1022/1024/1522/1524/2022/2024) do
 * not support 1 ns timebase setting.
 */
static const uint64_t timebases_hmo_compact[][2] = {
	/* nanoseconds */
	{ 2, 1000000000 },
	{ 5, 1000000000 },
	{ 10, 1000000000 },
	{ 20, 1000000000 },
	{ 50, 1000000000 },
	{ 100, 1000000000 },
	{ 200, 1000000000 },
	{ 500, 1000000000 },
	/* microseconds */
	{ 1, 1000000 },
	{ 2, 1000000 },
	{ 5, 1000000 },
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 50, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 500, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
};

/* RTO200x: from 25E-12 to 10000 s/div with 1E-12 increments */
static const uint64_t timebases_rto200x[][2] = {
	/* picoseconds */
	{ 25, 1000000000000 },
	{ 50, 1000000000000 },
	{ 100, 1000000000000 },
	{ 200, 1000000000000 },
	{ 500, 1000000000000 },
	/* nanoseconds */
	{ 1, 1000000000 },
	{ 2, 1000000000 },
	{ 5, 1000000000 },
	{ 10, 1000000000 },
	{ 20, 1000000000 },
	{ 50, 1000000000 },
	{ 100, 1000000000 },
	{ 200, 1000000000 },
	{ 500, 1000000000 },
	/* microseconds */
	{ 1, 1000000 },
	{ 2, 1000000 },
	{ 5, 1000000 },
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 50, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 500, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
	{ 100, 1 },
	{ 200, 1 },
	{ 500, 1 },
	{ 1000, 1 },
	{ 2000, 1 },
	{ 5000, 1 },
	{ 10000, 1 },
};

static const uint64_t vscale[][2] = {
	/* millivolts / div */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* volts / div */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
};

static const char *scope_analog_channel_names[] = {
	"CH1", "CH2", "CH3", "CH4",
};

static const char *scope_digital_channel_names[] = {
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

static struct scope_config scope_models[] = {
	{
		/* HMO Compact2: HMO722/1022/1522/2022 support only 8 digital channels. */
		.name = {"HMO722", "HMO1022", "HMO1522", "HMO2022", NULL},
		.analog_channels = 2,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate_nosegmem,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate_nosegmem),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig8_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig8_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_hmo,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_hmo),

		.timebases = &timebases_hmo_compact,
		.num_timebases = ARRAY_SIZE(timebases_hmo_compact),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		/* HMO1002/HMO1202 support only 8 digital channels. */
		.name = {"HMO1002", "HMO1202", NULL},
		.analog_channels = 2,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate_nosegmem,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate_nosegmem),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig8_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig8_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_hmo,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_hmo),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_scpi_dialect,
	},
	{
		/* RTC1002 supports only 8 digital channels. */
		.name = {"RTC1002", NULL},
		.analog_channels = 2,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate_nosegmem,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate_nosegmem),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig8_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig8_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		/* HMO3032/3042/3052/3522 support 16 digital channels. */
		.name = {"HMO3032", "HMO3042", "HMO3052", "HMO3522", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig16_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig16_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		/* FlatTop window available, but not listed in User Manual version 04. */
		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		/* HMO Compact4: HMO724/1024/1524/2024 support only 8 digital channels. */
		.name = {"HMO724", "HMO1024", "HMO1524", "HMO2024", NULL},
		.analog_channels = 4,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate_nosegmem,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate_nosegmem),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an4_dig8_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig8_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_hmo,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_hmo),

		.timebases = &timebases_hmo_compact,
		.num_timebases = ARRAY_SIZE(timebases_hmo_compact),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		.name = {"HMO2524", "HMO3034", "HMO3044", "HMO3054", "HMO3524", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an4_dig16_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_hmo,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_hmo),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		.name = {"RTB2002", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rtb200x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtb200x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an2_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		/* FFT support status unclear as of User Manual version 06. */
		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_log_not_pod_scpi_dialect,
	},
	{
		.name = {"RTB2004", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rtb200x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtb200x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an4_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		/* FFT support status unclear as of User Manual version 06. */
		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_log_not_pod_scpi_dialect,
	},
	{
		.name = {"RTM3002", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rtm300x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtm300x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an2_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_log_not_pod_scpi_dialect,
	},
	{
		.name = {"RTM3004", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rtm300x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtm300x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an4_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_log_not_pod_scpi_dialect,
	},
	{
		.name = {"RTA4004", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rtm300x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtm300x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an4_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		/* FFT support status unclear as of User Manual version 03. */
		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_log_not_pod_scpi_dialect,
	},
	{
		/* For RTO200x, number of analog channels is specified in the serial number, not in the name. */
		.name = {"RTO", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rto200x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rto200x),

		.logic_threshold = &logic_threshold_rto200x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rto200x),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &rto200x_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(rto200x_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_rto200x,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rto200x),

		.timebases = &timebases_rto200x,
		.num_timebases = ARRAY_SIZE(timebases_rto200x),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 10,

		.scpi_dialect = &rohde_schwarz_rto200x_scpi_dialect,
	},
};

static void scope_state_dump(const struct scope_config *config,
			     const struct scope_state *state)
{
	unsigned int i;
	char *tmp;

	for (i = 0; i < config->analog_channels; i++) {
		tmp = sr_voltage_per_div_string((*config->vscale)[state->analog_channels[i].vscale][0],
					     (*config->vscale)[state->analog_channels[i].vscale][1]);
		sr_info("State of analog channel %d -> %s : %s (coupling) %s (vscale) %2.2e (offset)",
			i + 1, state->analog_channels[i].state ? "On" : "Off",
			(*config->coupling_options)[state->analog_channels[i].coupling],
			tmp, state->analog_channels[i].vertical_offset);
	}

	for (i = 0; i < config->digital_channels; i++) {
		sr_info("State of digital channel %d -> %s", i,
			state->digital_channels[i] ? "On" : "Off");
	}

	for (i = 0; i < config->digital_pods; i++) {
		if (!strncmp("USER", (*config->logic_threshold)[state->digital_pods[i].threshold], 4) ||
		    !strcmp("MAN", (*config->logic_threshold)[state->digital_pods[i].threshold]))
			sr_info("State of digital POD %d -> %s : %E (threshold)", i + 1,
				state->digital_pods[i].state ? "On" : "Off",
				state->digital_pods[i].user_threshold);
		else
			sr_info("State of digital POD %d -> %s : %s (threshold)", i + 1,
				state->digital_pods[i].state ? "On" : "Off",
				(*config->logic_threshold)[state->digital_pods[i].threshold]);
	}

	tmp = sr_period_string((*config->timebases)[state->timebase][0],
			       (*config->timebases)[state->timebase][1]);
	sr_info("Current timebase: %s", tmp);
	g_free(tmp);

	tmp = sr_samplerate_string(state->sample_rate);
	sr_info("Current samplerate: %s", tmp);
	g_free(tmp);

	if (!strcmp("PATT", (*config->trigger_sources)[state->trigger_source]))
		sr_info("Current trigger: %s (pattern), %.2f (offset)",
			state->trigger_pattern,
			state->horiz_triggerpos);
	else // Edge (slope) trigger
		sr_info("Current trigger: %s (source), %s (slope) %.2f (offset)",
			(*config->trigger_sources)[state->trigger_source],
			(*config->trigger_slopes)[state->trigger_slope],
			state->horiz_triggerpos);
}

static int scope_state_get_array_option(struct sr_scpi_dev_inst *scpi,
		const char *command, const char *(*array)[], const unsigned int n, unsigned int *result)
{
	char *tmp;
	int idx;

	if (sr_scpi_get_string(scpi, command, &tmp) != SR_OK)
		return SR_ERR;

	if ((idx = std_str_idx_s(tmp, *array, n)) < 0) {
		g_free(tmp);
		return SR_ERR_ARG;
	}

	*result = idx;

	g_free(tmp);

	return SR_OK;
}

/**
 * This function takes a value of the form "2.000E-03" and returns the index
 * of an array where a matching pair was found.
 *
 * @param value The string to be parsed.
 * @param array The array of s/f pairs.
 * @param array_len The number of pairs in the array.
 * @param result The index at which a matching pair was found.
 *
 * @return SR_ERR on any parsing error, SR_OK otherwise.
 */
static int array_float_get(gchar *value, const uint64_t array[][2],
		const int array_len, unsigned int *result)
{
	struct sr_rational rval;
	struct sr_rational aval;

	if (sr_parse_rational(value, &rval) != SR_OK)
		return SR_ERR;

	for (int i = 0; i < array_len; i++) {
		sr_rational_set(&aval, array[i][0], array[i][1]);
		if (sr_rational_eq(&rval, &aval)) {
			*result = i;
			return SR_OK;
		}
	}

	return SR_ERR;
}

static struct sr_channel *get_channel_by_index_and_type(GSList *channel_lhead,
							const int index, const int type)
{
	while (channel_lhead) {
		struct sr_channel *ch = channel_lhead->data;
		if (ch->index == index && ch->type == type)
			return ch;

		channel_lhead = channel_lhead->next;
	}

	return 0;
}

static int analog_channel_state_get(const struct sr_dev_inst *sdi,
				    const struct scope_config *config,
				    struct scope_state *state)
{
	unsigned int i, j;
	char command[MAX_COMMAND_SIZE];
	char *tmp_str;
	struct sr_channel *ch;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	for (i = 0; i < config->analog_channels; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_ANALOG_CHAN_STATE],
			   i + 1);

		if (sr_scpi_get_bool(scpi, command,
				     &state->analog_channels[i].state) != SR_OK)
			return SR_ERR;

		ch = get_channel_by_index_and_type(sdi->channels, i, SR_CHANNEL_ANALOG);
		if (ch)
			ch->enabled = state->analog_channels[i].state;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_VERTICAL_SCALE],
			   i + 1);

		if (sr_scpi_get_string(scpi, command, &tmp_str) != SR_OK)
			return SR_ERR;

		if (array_float_get(tmp_str, ARRAY_AND_SIZE(vscale), &j) != SR_OK) {
			g_free(tmp_str);
			sr_err("Could not determine array index for vertical div scale.");
			return SR_ERR;
		}

		g_free(tmp_str);
		state->analog_channels[i].vscale = j;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_VERTICAL_OFFSET],
			   i + 1);

		if (sr_scpi_get_float(scpi, command,
				     &state->analog_channels[i].vertical_offset) != SR_OK)
			return SR_ERR;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_COUPLING],
			   i + 1);

		if (config->coupling_options && config->num_coupling_options)
			if (scope_state_get_array_option(scpi, command, config->coupling_options,
						 config->num_coupling_options,
						 &state->analog_channels[i].coupling) != SR_OK)
				return SR_ERR;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_PROBE_UNIT],
			   i + 1);

		if (sr_scpi_get_string(scpi, command, &tmp_str) != SR_OK)
			return SR_ERR;

		if (tmp_str[0] == 'A')
			state->analog_channels[i].probe_unit = 'A';
		else
			state->analog_channels[i].probe_unit = 'V';
		g_free(tmp_str);
	}

	return SR_OK;
}

static int digital_channel_state_get(const struct sr_dev_inst *sdi,
				     const struct scope_config *config,
				     struct scope_state *state)
{
	unsigned int i, idx, nibble1ch2, nibble2ch2, tmp_uint;
	int result = SR_ERR;
	char *logic_threshold_short[MAX_NUM_LOGIC_THRESHOLD_ENTRIES];
	char command[MAX_COMMAND_SIZE];
	struct sr_channel *ch;
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	float tmp_float;

	for (i = 0; i < config->digital_channels; i++) {
		if (strncmp("RTO", sdi->model, 3))
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_CHAN_STATE],
				   i);
		else
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_CHAN_STATE],
				   (i / DIGITAL_CHANNELS_PER_POD) + 1, i);

		if (sr_scpi_get_bool(scpi, command,
				     &state->digital_channels[i]) != SR_OK)
			return SR_ERR;

		ch = get_channel_by_index_and_type(sdi->channels, i, SR_CHANNEL_LOGIC);
		if (ch)
			ch->enabled = state->digital_channels[i];
	}

	/* According to the SCPI standard, on models that support multiple
	 * user-defined logic threshold settings the response to the command
	 * SCPI_CMD_GET_DIG_POD_THRESHOLD might return "USER" instead of
	 * "USER1".
	 *
	 * This makes more difficult to validate the response when the logic
	 * threshold is set to "USER1" and therefore we need to prevent device
	 * opening failures in such configuration case...
	 */
	for (i = 0; i < config->num_logic_threshold; i++) {
		logic_threshold_short[i] = g_strdup((*config->logic_threshold)[i]);
		if (!strcmp("USER1", (*config->logic_threshold)[i]))
			g_strlcpy(logic_threshold_short[i],
				  (*config->logic_threshold)[i], strlen((*config->logic_threshold)[i]));
	}

	for (i = 0; i < config->digital_pods; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_STATE],
			   i + 1);

		if (sr_scpi_get_bool(scpi, command,
				     &state->digital_pods[i].state) != SR_OK)
			goto exit;

		if (config->logic_threshold && config->num_logic_threshold) {
			/* Check if the threshold command is based on the POD or nibble channel index. */
			if (config->logic_threshold_for_pod) {
				idx = i + 1;
			} else {
				nibble1ch2 = i * DIGITAL_CHANNELS_PER_POD + 1;
				nibble2ch2 = (i + 1) * DIGITAL_CHANNELS_PER_POD - DIGITAL_CHANNELS_PER_NIBBLE + 1;
				idx = nibble1ch2;
			}
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_THRESHOLD],
				   idx);

			/* Check for both standard and shortened responses. */
			if (scope_state_get_array_option(scpi, command, config->logic_threshold,
							 config->num_logic_threshold,
							 &state->digital_pods[i].threshold) != SR_OK)
				if (scope_state_get_array_option(scpi, command, (const char * (*)[]) &logic_threshold_short,
								 config->num_logic_threshold,
								 &state->digital_pods[i].threshold) != SR_OK)
					goto exit;

			/* Same as above, but for the second nibble (second channel), if needed. */
			if (!config->logic_threshold_for_pod) {
				if (scope_state_get_array_option(scpi, command, config->logic_threshold,
								 config->num_logic_threshold,
								 &tmp_uint) != SR_OK)
					if (scope_state_get_array_option(scpi, command, (const char * (*)[]) &logic_threshold_short,
									 config->num_logic_threshold,
									 &tmp_uint) != SR_OK)
						goto exit;

				/* If the two nibbles don't match, use the first one. */
				if (state->digital_pods[i].threshold != tmp_uint) {
					g_snprintf(command, sizeof(command),
						   (*config->scpi_dialect)[SCPI_CMD_SET_DIG_POD_THRESHOLD],
						   nibble2ch2,
						   (*config->logic_threshold)[state->digital_pods[i].threshold]);
					if (sr_scpi_send(sdi->conn, command) != SR_OK ||
					    sr_scpi_get_opc(sdi->conn) != SR_OK)
						goto exit;
				}
			}

			/* If used-defined or custom threshold is active, get the level. */
			if (!strcmp("USER1", (*config->logic_threshold)[state->digital_pods[i].threshold])) {
				g_snprintf(command, sizeof(command),
					   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
					   idx, 1); /* USER1 logic threshold setting. */
			} else if (!strcmp("USER2", (*config->logic_threshold)[state->digital_pods[i].threshold])) {
				g_snprintf(command, sizeof(command),
					   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
					   idx, 2); /* USER2 for custom logic_threshold setting. */
			} else if (!strcmp("USER", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
				   !strcmp("MAN", (*config->logic_threshold)[state->digital_pods[i].threshold])) {
				if (strncmp("RTO", sdi->model, 3)) {
					g_snprintf(command, sizeof(command),
						   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
						   idx); /* USER or MAN for custom logic_threshold setting. */
				} else { /* The RTO200x divides each POD in two channel groups. */
					g_snprintf(command, sizeof(command),
						   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
						   idx, idx * 2); /* MAN setting on the second channel group. */
					if (sr_scpi_get_float(scpi, command,
					    &tmp_float) != SR_OK)
						goto exit;
					g_snprintf(command, sizeof(command),
						   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
						   idx, idx * 2 - 1); /* MAN setting on the first channel group. */
				}
			}
			if (!strcmp("USER1", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
			    !strcmp("USER2", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
			    !strcmp("USER", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
			    !strcmp("MAN", (*config->logic_threshold)[state->digital_pods[i].threshold])) {
				if (sr_scpi_get_float(scpi, command,
				    &state->digital_pods[i].user_threshold) != SR_OK)
					goto exit;

				/* Set the same custom threshold on the second nibble, if needed. */
				if (!config->logic_threshold_for_pod) {
					g_snprintf(command, sizeof(command),
						   (*config->scpi_dialect)[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD],
						   nibble2ch2,
						   (*config->logic_threshold)[state->digital_pods[i].threshold]);
					if (sr_scpi_send(sdi->conn, command) != SR_OK ||
					    sr_scpi_get_opc(sdi->conn) != SR_OK)
						goto exit;
				}

				/* On the RTO200x set the same custom threshold on both channel groups of each POD. */
				if (!strncmp("RTO", sdi->model, 3)) {
					if (state->digital_pods[i].user_threshold != tmp_float) {
						g_snprintf(command, sizeof(command),
							   (*config->scpi_dialect)[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD],
							   idx, idx * 2, state->digital_pods[i].user_threshold);
						if (sr_scpi_send(sdi->conn, command) != SR_OK ||
						    sr_scpi_get_opc(sdi->conn) != SR_OK)
							goto exit;
					}
				}
			}
		}
	}

	result = SR_OK;

exit:
	for (i = 0; i < config->num_logic_threshold; i++)
		g_free(logic_threshold_short[i]);

	return result;
}

SR_PRIV int hmo_update_sample_rate(const struct sr_dev_inst *sdi)
{
	const struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	float tmp_float;

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	if (sr_scpi_get_float(sdi->conn,
			      (*config->scpi_dialect)[SCPI_CMD_GET_SAMPLE_RATE],
			      &tmp_float) != SR_OK)
		return SR_ERR;

	state->sample_rate = tmp_float;

	return SR_OK;
}

SR_PRIV int hmo_scope_state_get(const struct sr_dev_inst *sdi)
{
	const struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	float tmp_float;
	unsigned int i;
	char *tmp_str, *tmp_str2;
	char command[MAX_COMMAND_SIZE];

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	sr_info("Fetching scope state");

	/* Save existing Math Expression. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_MATH_EXPRESSION],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_string(sdi->conn, command, &tmp_str) != SR_OK)
		return SR_ERR;

	strncpy(state->restore_math_expr,
		sr_scpi_unquote_string(tmp_str),
		MAX_COMMAND_SIZE);
	g_free(tmp_str);

	/* If the oscilloscope is currently in FFT mode, switch to normal mode. */
	if (!strncmp(FFT_MATH_EXPRESSION, state->restore_math_expr, strlen(FFT_MATH_EXPRESSION))) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_SET_MATH_EXPRESSION],
			   MATH_WAVEFORM_INDEX, FFT_EXIT_MATH_EXPRESSION);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK) {
			sr_err("Failed to disable the FFT mode!");
				return SR_ERR;
		}
	}

	if (analog_channel_state_get(sdi, config, state) != SR_OK)
		return SR_ERR;

	if (digital_channel_state_get(sdi, config, state) != SR_OK)
		return SR_ERR;

	if (sr_scpi_get_string(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TIMEBASE],
			&tmp_str) != SR_OK)
		return SR_ERR;

	if (array_float_get(tmp_str, ARRAY_AND_SIZE(timebases), &i) != SR_OK) {
		g_free(tmp_str);
		sr_err("Could not determine array index for time base.");
		return SR_ERR;
	}
	g_free(tmp_str);

	state->timebase = i;

	/* Determine the number of horizontal (x) divisions. */
	if (sr_scpi_get_int(sdi->conn,
	    (*config->scpi_dialect)[SCPI_CMD_GET_HORIZONTAL_DIV],
	    (int *)&config->num_xdivs) != SR_OK)
		return SR_ERR;

	/* Not all models allow setting the mode for waveform acquisition
	 * rate and sample rate configuration.
	 */
	if (config->waveform_sample_rate && config->num_waveform_sample_rate &&
	    (*config->scpi_dialect)[SCPI_CMD_GET_WAVEFORM_SAMPLE_RATE]) {
		if (scope_state_get_array_option(sdi->conn,
						 (*config->scpi_dialect)[SCPI_CMD_GET_WAVEFORM_SAMPLE_RATE],
						 config->waveform_sample_rate, config->num_waveform_sample_rate,
						 &state->waveform_sample_rate) != SR_OK)
			return SR_ERR;
	}

	if (scope_state_get_array_option(sdi->conn,
					 (*config->scpi_dialect)[SCPI_CMD_GET_INTERPOLATION_MODE],
					 config->interpolation_mode, config->num_interpolation_mode,
					 &state->interpolation_mode) != SR_OK)
		return SR_ERR;

	if (sr_scpi_get_float(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_HORIZ_TRIGGERPOS],
			&tmp_float) != SR_OK)
		return SR_ERR;
	state->horiz_triggerpos = tmp_float /
		(((double) (*config->timebases)[state->timebase][0] /
		  (*config->timebases)[state->timebase][1]) * config->num_xdivs);
	state->horiz_triggerpos -= 0.5;
	state->horiz_triggerpos *= -1;

	if (scope_state_get_array_option(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_SOURCE],
			config->trigger_sources, config->num_trigger_sources,
			&state->trigger_source) != SR_OK)
		return SR_ERR;

	if (scope_state_get_array_option(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_SLOPE],
			config->trigger_slopes, config->num_trigger_slopes,
			&state->trigger_slope) != SR_OK)
		return SR_ERR;

	if (strncmp("RTO", sdi->model, 3)) {
		if (sr_scpi_get_string(sdi->conn,
				       (*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_PATTERN],
				       &tmp_str) != SR_OK)
			return SR_ERR;
	} else { /* RTO200x: A separate command needs to be issued for each bit in the pattern. */
		tmp_str = g_malloc0_n(MAX_TRIGGER_PATTERN_LENGTH, sizeof(char));
		if (!tmp_str)
			return SR_ERR_MALLOC;
		for (i = 0; i < DIGITAL_CHANNELS_PER_POD * config->digital_pods &&
		     i < MAX_TRIGGER_PATTERN_LENGTH; i++) {
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_PATTERN],
				   i);
			if (sr_scpi_get_string(sdi->conn, command, &tmp_str2))
				return SR_ERR;
			if (!strcmp("LOW", tmp_str2)) {
				tmp_str[i] = '0';
			} else if (!strcmp("HIGH", tmp_str2)) {
				tmp_str[i] = '1';
			} else {
				tmp_str[i] = 'X';
			}
			g_free(tmp_str2);
		}
	}
	strncpy(state->trigger_pattern,
		sr_scpi_unquote_string(tmp_str),
		MAX_TRIGGER_PATTERN_LENGTH);
	g_free(tmp_str);

	/* Not currently implemented on RTO200x. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_HIGH_RESOLUTION]) {
		if (sr_scpi_get_string(sdi->conn,
				     (*config->scpi_dialect)[SCPI_CMD_GET_HIGH_RESOLUTION],
				     &tmp_str) != SR_OK)
			return SR_ERR;
		if (!strcmp("OFF", tmp_str))
			state->high_resolution = FALSE;
		else
			state->high_resolution = TRUE;
		g_free(tmp_str);
	}

	/* Not currently implemented on RTO200x. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_PEAK_DETECTION]) {
		if (sr_scpi_get_string(sdi->conn,
				     (*config->scpi_dialect)[SCPI_CMD_GET_PEAK_DETECTION],
				     &tmp_str) != SR_OK)
			return SR_ERR;
		if (!strcmp("OFF", tmp_str))
			state->peak_detection = FALSE;
		else
			state->peak_detection = TRUE;
		g_free(tmp_str);
	}

	/* Determine the FFT window type. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_WINDOW_TYPE],
		   MATH_WAVEFORM_INDEX);

	if (scope_state_get_array_option(sdi->conn, command,
					 config->fft_window_types, config->num_fft_window_types,
					 &state->fft_window_type) != SR_OK)
		return SR_ERR;

	/* Determine the FFT start frequency. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_FREQUENCY_START],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &state->fft_freq_start) != SR_OK)
		return SR_ERR;

	/* Determine the FFT stop frequency. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_FREQUENCY_STOP],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &state->fft_freq_stop) != SR_OK)
		return SR_ERR;

	/* Determine the FFT frequency span. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_FREQUENCY_SPAN],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &state->fft_freq_span) != SR_OK)
		return SR_ERR;

	/* Determine the FFT center frequency. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_FREQUENCY_CENTER],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &state->fft_freq_center) != SR_OK)
		return SR_ERR;

	/* Determine the FFT Resolution Bandwidth. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_RESOLUTION_BW],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &state->fft_rbw) != SR_OK)
		return SR_ERR;

	/* Determine the FFT Resolution Bandwidth / Span coupling. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_SPAN_RBW_COUPLING],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_bool(sdi->conn, command,
			     &state->fft_span_rbw_coupling) != SR_OK)
		return SR_ERR;

	/* Determine the FFT Resolution Bandwidth / Span ratio. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_SPAN_RBW_RATIO],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &tmp_float) != SR_OK)
		return SR_ERR;

	state->fft_span_rbw_ratio = tmp_float;

	if (hmo_update_sample_rate(sdi) != SR_OK)
		return SR_ERR;

	sr_info("Fetching finished.");

	scope_state_dump(config, state);

	return SR_OK;
}

static struct scope_state *scope_state_new(const struct scope_config *config)
{
	struct scope_state *state = NULL;

	state = g_malloc0(sizeof(struct scope_state));
	if (!state)
		return state;

	state->analog_channels = g_malloc0_n(config->analog_channels,
			sizeof(struct analog_channel_state));
	state->digital_channels = g_malloc0_n(
			config->digital_channels, sizeof(gboolean));
	state->digital_pods = g_malloc0_n(config->digital_pods,
			sizeof(struct digital_pod_state));

	if (!state->analog_channels || !state->digital_channels || !state->digital_pods) {
		rs_scope_state_free(state);
		return NULL;
	}

	return state;
}

SR_PRIV void hmo_scope_state_free(struct scope_state *state)
{
	if (!state)
		return;

	g_free(state->analog_channels);
	g_free(state->digital_channels);
	g_free(state->digital_pods);
	g_free(state);
}

SR_PRIV int hmo_init_device(struct sr_dev_inst *sdi)
{
	int model_index;
	unsigned int i, j, group;
	struct sr_channel *ch;
	struct dev_context *devc;
	char *tmp_str;
	int ret;

	if (!sdi)
		return SR_ERR;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR;

	model_index = -1;

	/* Find the exact model. */
	for (i = 0; i < ARRAY_SIZE(scope_models); i++) {
		for (j = 0; scope_models[i].name[j]; j++) {
			if (!strcmp(sdi->model, scope_models[i].name[j])) {
				model_index = i;
				break;
			}
		}
		if (model_index != -1)
			break;
	}

	if (model_index == -1) {
		sr_dbg("Unsupported device.");
		return SR_ERR_NA;
	}

	/*
	 * Configure the number of analog channels (2 or 4) from the last
	 * digit of the serial number on the RTO200x (1329.7002k[0-4][24]).
	 */
	if (!strncmp("RTO", sdi->model, 3)) {
		i = strlen(sdi->serial_num);
		scope_models[model_index].analog_channels = 2;
		if (i >= 12)
			if (sdi->serial_num[11] == '4')
				scope_models[model_index].analog_channels = 4;
	}

	/* Configure the number of PODs given the number of digital channels. */
	scope_models[model_index].digital_pods = scope_models[model_index].digital_channels / DIGITAL_CHANNELS_PER_POD;

	devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group*) *
					scope_models[model_index].analog_channels);
	devc->digital_groups = g_malloc0(sizeof(struct sr_channel_group*) *
					 scope_models[model_index].digital_pods);
	if (!devc->analog_groups || !devc->digital_groups) {
		g_free(devc->analog_groups);
		g_free(devc->digital_groups);
		return SR_ERR_MALLOC;
	}

	/* Add analog channels. */
	for (i = 0; i < scope_models[model_index].analog_channels; i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE,
			   (*scope_models[model_index].analog_names)[i]);

		devc->analog_groups[i] = g_malloc0(sizeof(struct sr_channel_group));

		devc->analog_groups[i]->name = g_strdup(
			(char *)(*scope_models[model_index].analog_names)[i]);
		devc->analog_groups[i]->channels = g_slist_append(NULL, ch);

		sdi->channel_groups = g_slist_append(sdi->channel_groups,
						   devc->analog_groups[i]);
	}

	/* Add digital channel groups. */
	ret = SR_OK;
	for (i = 0; i < scope_models[model_index].digital_pods; i++) {
		devc->digital_groups[i] = g_malloc0(sizeof(struct sr_channel_group));
		if (!devc->digital_groups[i]) {
			ret = SR_ERR_MALLOC;
			break;
		}
		devc->digital_groups[i]->name = g_strdup_printf("POD%d", i + 1);
		sdi->channel_groups = g_slist_append(sdi->channel_groups,
				   devc->digital_groups[i]);
	}
	if (ret != SR_OK)
		return ret;

	/* Add digital channels. */
	for (i = 0; i < scope_models[model_index].digital_channels; i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
			   (*scope_models[model_index].digital_names)[i]);

		group = i / DIGITAL_CHANNELS_PER_POD;
		devc->digital_groups[group]->channels = g_slist_append(
			devc->digital_groups[group]->channels, ch);
	}

	/* Add special channels for the Fast Fourier Transform (FFT). */
	for (i = 0; i < scope_models[model_index].analog_channels; i++) {
		tmp_str = g_strdup_printf("FFT_CH%d", i + 1);
		ch = sr_channel_new(sdi, i, SR_CHANNEL_FFT, TRUE, tmp_str);
		g_free(tmp_str);
	}

	devc->model_config = &scope_models[model_index];
	devc->samples_limit = 0;
	devc->frame_limit = 0;

	if (!(devc->model_state = scope_state_new(devc->model_config)))
		return SR_ERR_MALLOC;

	return SR_OK;
}

/* Queue data of one channel group, for later submission. */
SR_PRIV void hmo_queue_logic_data(struct dev_context *devc,
				  const size_t group, const GByteArray *pod_data)
{
	size_t size;
	GByteArray *store;
	uint8_t *logic_data;
	size_t idx, logic_step;

	if (!devc || !pod_data)
		return;

	/*
	 * Upon first invocation, allocate the array which can hold the
	 * combined logic data for all channels. Assume that each channel
	 * will yield an identical number of samples per receive call.
	 *
	 * As a poor man's safety measure: (Silently) skip processing
	 * for unexpected sample counts, and ignore samples for
	 * unexpected channel groups. Don't bother with complicated
	 * resize logic, considering that many models only support one
	 * pod, and the most capable supported models have two pods of
	 * identical size. We haven't yet seen any "odd" configuration.
	 */
	if (!devc->logic_data) {
		size = pod_data->len * devc->pod_count;
		store = g_byte_array_sized_new(size);
		memset(store->data, 0, size);
		store = g_byte_array_set_size(store, size);
		devc->logic_data = store;
	} else {
		store = devc->logic_data;
		size = store->len / devc->pod_count;
		if (group >= devc->pod_count)
			return;
	}

	/*
	 * Fold the data of the most recently received channel group into
	 * the storage, where data resides for all channels combined.
	 */
	logic_data = store->data;
	logic_data += group;
	logic_step = devc->pod_count;
	for (idx = 0; idx < pod_data->len; idx++) {
		*logic_data = pod_data->data[idx];
		logic_data += logic_step;
	}

	/* Truncate acquisition if a smaller number of samples has been requested. */
	if (devc->samples_limit > 0 && devc->logic_data->len > devc->samples_limit * devc->pod_count)
		devc->logic_data->len = devc->samples_limit * devc->pod_count;
}

/* Submit data for all channels, after the individual groups got collected. */
SR_PRIV void hmo_send_logic_packet(const struct sr_dev_inst *sdi,
				   const struct dev_context *devc)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	if (!sdi || !devc)
		return;

	if (!devc->logic_data)
		return;

	logic.data = devc->logic_data->data;
	logic.length = devc->logic_data->len;
	logic.unitsize = devc->pod_count;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;

	sr_session_send(sdi, &packet);
}

/* Undo previous resource allocation. */
SR_PRIV void hmo_cleanup_logic_data(struct dev_context *devc)
{
	if (!devc)
		return;

	if (devc->logic_data) {
		g_byte_array_free(devc->logic_data, TRUE);
		devc->logic_data = NULL;
	}

	/*
	 * Keep 'pod_count'! It's required when more frames will be
	 * received, and does not harm when kept after acquisition.
	 */
}

SR_PRIV int hmo_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_channel *ch;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	const struct scope_state *state;
	struct sr_datafeed_packet packet;
	GByteArray *data;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_datafeed_logic logic;
	size_t group;

	(void)fd;
	(void)revents;

	data = NULL;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	/* Although this is correct in general, the USBTMC libusb implementation
	 * currently does not generate an event prior to the first read. Often
	 * it is ok to start reading just after the 50ms timeout. See bug #785.
	if (revents != G_IO_IN)
		return TRUE;
	*/

	ch = devc->current_channel->data;
	state = devc->model_state;

	/*
	 * Send "frame begin" packet upon reception of data for the
	 * first enabled channel.
	 */
	if (devc->current_channel == devc->enabled_channels) {
		packet.type = SR_DF_FRAME_BEGIN;
		sr_session_send(sdi, &packet);
	}

	/*
	 * Pass on the received data of the channel(s).
	 */
	switch (ch->type) {
	case SR_CHANNEL_ANALOG:
	case SR_CHANNEL_FFT:
		if (sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK) {
			if (data)
				g_byte_array_free(data, TRUE);
			return TRUE;
		}

		packet.type = SR_DF_ANALOG;

		analog.data = data->data;
		analog.num_samples = data->len / sizeof(float);
		/* Truncate acquisition if a smaller number of samples has been requested. */
		if (devc->samples_limit > 0 && analog.num_samples > devc->samples_limit)
			analog.num_samples = devc->samples_limit;
		analog.encoding = &encoding;
		analog.meaning = &meaning;
		analog.spec = &spec;

		encoding.unitsize = sizeof(float);
		encoding.is_signed = TRUE;
		encoding.is_float = TRUE;
#ifdef WORDS_BIGENDIAN
		encoding.is_bigendian = TRUE;
#else
		encoding.is_bigendian = FALSE;
#endif
		/* TODO: Use proper 'digits' value for this device (and its modes). */
		encoding.digits = 2;
		encoding.is_digits_decimal = FALSE;
		encoding.scale.p = 1;
		encoding.scale.q = 1;
		encoding.offset.p = 0;
		encoding.offset.q = 1;
		if (ch->type == SR_CHANNEL_ANALOG) {
			if (state) {
				if (state->analog_channels[ch->index].probe_unit == 'V') {
					meaning.mq = SR_MQ_VOLTAGE;
					meaning.unit = SR_UNIT_VOLT;
				} else {
					meaning.mq = SR_MQ_CURRENT;
					meaning.unit = SR_UNIT_AMPERE;
				}
			}
		} else if (ch->type == SR_CHANNEL_FFT) {
			meaning.mq = SR_MQ_POWER;
			meaning.unit = SR_UNIT_DECIBEL_MW;
		}
		meaning.mqflags = 0;
		meaning.channels = g_slist_append(NULL, ch);
		/* TODO: Use proper 'digits' value for this device (and its modes). */
		spec.spec_digits = 2;
		packet.payload = &analog;
		sr_session_send(sdi, &packet);
		devc->num_samples = data->len / sizeof(float);
		g_slist_free(meaning.channels);
		g_byte_array_free(data, TRUE);
		data = NULL;
		break;
	case SR_CHANNEL_LOGIC:
		if (sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK) {
			if (data)
				g_byte_array_free(data, TRUE);
			return TRUE;
		}

		/*
		 * If only data from the first pod is involved in the
		 * acquisition, then the raw input bytes can get passed
		 * forward for performance reasons. When the second pod
		 * is involved (either alone, or in combination with the
		 * first pod), then the received bytes need to be put
		 * into memory in such a layout that all channel groups
		 * get combined, and a unitsize larger than a single byte
		 * applies. The "queue" logic transparently copes with
		 * any such configuration. This works around the lack
		 * of support for "meaning" to logic data, which is used
		 * above for analog data.
		 */
		if (devc->pod_count == 1) {
			packet.type = SR_DF_LOGIC;
			logic.data = data->data;
			logic.length = data->len;
			/* Truncate acquisition if a smaller number of samples has been requested. */
			if (devc->samples_limit > 0 && logic.length > devc->samples_limit)
				logic.length = devc->samples_limit;
			logic.unitsize = 1;
			packet.payload = &logic;
			sr_session_send(sdi, &packet);
		} else {
			group = ch->index / DIGITAL_CHANNELS_PER_POD;
			hmo_queue_logic_data(devc, group, data);
		}

		devc->num_samples = data->len / devc->pod_count;
		g_byte_array_free(data, TRUE);
		data = NULL;
		break;
	default:
		sr_err("Invalid channel type.");
		break;
	}

	/*
	 * Advance to the next enabled channel. When data for all enabled
	 * channels was received, then flush potentially queued logic data,
	 * and send the "frame end" packet.
	 */
	if (devc->current_channel->next) {
		devc->current_channel = devc->current_channel->next;
		hmo_request_data(sdi);
		return TRUE;
	}
	hmo_send_logic_packet(sdi, devc);

	/*
	 * Release the logic data storage after each frame. This copes
	 * with sample counts that differ in length per frame. -- Is
	 * this a real constraint when acquiring multiple frames with
	 * identical device settings?
	 */
	hmo_cleanup_logic_data(devc);

	packet.type = SR_DF_FRAME_END;
	sr_session_send(sdi, &packet);

	/*
	 * End of frame was reached. Stop acquisition after the specified
	 * number of frames or after the specified number of samples, or
	 * continue reception by starting over at the first enabled channel.
	 */
	if (++devc->num_frames >= devc->frame_limit || devc->num_samples >= devc->samples_limit) {
		sr_dev_acquisition_stop(sdi);
		hmo_cleanup_logic_data(devc);
	} else {
		devc->current_channel = devc->enabled_channels;
		hmo_request_data(sdi);
	}

	return TRUE;
}
