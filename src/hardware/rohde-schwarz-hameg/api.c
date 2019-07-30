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
#include <stdlib.h>
#include <unistd.h>
#include "scpi.h"
#include "protocol.h"

static struct sr_dev_driver rohde_schwarz_driver_info;

static const char *manufacturers[] = {
	"Rohde&Schwarz",
	"HAMEG",
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LOGIC_ANALYZER,
};

enum {
	CG_INVALID = -1,
	CG_NONE,
	CG_ANALOG,
	CG_DIGITAL,
};

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;

	sdi = NULL;
	devc = NULL;
	hw_info = NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response.");
		goto fail;
	}

	if (std_str_idx_s(hw_info->manufacturer, ARRAY_AND_SIZE(manufacturers)) < 0)
		goto fail;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	if (!sdi)
		goto fail;

	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->driver = &rohde_schwarz_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->conn = scpi;

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	devc = g_malloc0(sizeof(struct dev_context));
	if (!devc)
		goto fail;

	sdi->priv = devc;

	if (rs_init_device(sdi) != SR_OK)
		goto fail;

	return sdi;

fail:
	sr_scpi_hw_info_free(hw_info);
	sr_dev_inst_free(sdi);
	g_free(devc);

	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

static void clear_helper(struct dev_context *devc)
{
	rs_scope_state_free(devc->model_state);
	g_free(devc->analog_groups);
	g_free(devc->digital_groups);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	if (!sdi)
		return SR_ERR;

	if (sr_scpi_open(sdi->conn) != SR_OK)
		return SR_ERR;

	if (rs_scope_state_get(sdi) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	if (!sdi)
		return SR_ERR;

	return sr_scpi_close(sdi->conn);
}

static int check_channel_group(const struct dev_context *devc,
			     const struct sr_channel_group *cg)
{
	const struct scope_config *model;

	if (!devc)
		return CG_INVALID;

	model = devc->model_config;

	if (!cg)
		return CG_NONE;

	if (std_cg_idx(cg, devc->analog_groups, model->analog_channels) >= 0)
		return CG_ANALOG;

	if (std_cg_idx(cg, devc->digital_groups, model->digital_pods) >= 0)
		return CG_DIGITAL;

	sr_err("Invalid channel group specified.");

	return CG_INVALID;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int cg_type, idx, i;
	const struct dev_context *devc;
	const struct scope_config *model;
	const struct scope_state *state;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	model = devc->model_config;
	if (!model)
		return SR_ERR_ARG;

	state = devc->model_state;

	switch (key) {
	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(model->num_xdivs);
		break;
	case SR_CONF_TIMEBASE:
		if (!model->timebases || !model->num_timebases)
			return SR_ERR_NA;
		*data = g_variant_new("(tt)", (*model->timebases)[state->timebase][0],
				      (*model->timebases)[state->timebase][1]);
		break;
	case SR_CONF_NUM_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if (std_cg_idx(cg, devc->analog_groups, model->analog_channels) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_int32(model->num_ydivs);
		break;
	case SR_CONF_VSCALE:
		if (!model->vscale || !model->num_vscale)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if ((idx = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new("(tt)",
				      (*model->vscale)[state->analog_channels[idx].vscale][0],
				      (*model->vscale)[state->analog_channels[idx].vscale][1]);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!model->trigger_sources || !model->num_trigger_sources)
			return SR_ERR_NA;
		*data = g_variant_new_string((*model->trigger_sources)[state->trigger_source]);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (!model->trigger_slopes || !model->num_trigger_slopes)
			return SR_ERR_NA;
		*data = g_variant_new_string((*model->trigger_slopes)[state->trigger_slope]);
		break;
	case SR_CONF_TRIGGER_PATTERN:
		*data = g_variant_new_string(state->trigger_pattern);
		break;
	case SR_CONF_HIGH_RESOLUTION:
		/* Not currently implemented on RTO200x. */
		if (!(*model->scpi_dialect)[SCPI_CMD_GET_HIGH_RESOLUTION])
			return SR_ERR_NA;
		*data = g_variant_new_boolean(state->high_resolution);
		break;
	case SR_CONF_PEAK_DETECTION:
		/* Not currently implemented on RTO200x. */
		if (!(*model->scpi_dialect)[SCPI_CMD_GET_PEAK_DETECTION])
			return SR_ERR_NA;
		*data = g_variant_new_boolean(state->peak_detection);
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		*data = g_variant_new_double(state->horiz_triggerpos);
		break;
	case SR_CONF_COUPLING:
		if (!model->coupling_options || !model->num_coupling_options)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if ((idx = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_string((*model->coupling_options)[state->analog_channels[idx].coupling]);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(state->sample_rate);
		break;
        case SR_CONF_WAVEFORM_SAMPLE_RATE:
		/* Make sure it is supported by the specific model. */
		if (!model->waveform_sample_rate || !model->num_waveform_sample_rate)
			return SR_ERR_NA;
		if (!(*model->scpi_dialect)[SCPI_CMD_GET_WAVEFORM_SAMPLE_RATE])
			return SR_ERR_NA;
		*data = g_variant_new_string((*model->waveform_sample_rate)[state->waveform_sample_rate]);
		break;
	case SR_CONF_AUTO_RECORD_LENGTH:
		/* Only supported on the RTB2000, RTM3000 and RTA4000. */
		if (!(*model->scpi_dialect)[SCPI_CMD_GET_AUTO_RECORD_LENGTH])
			return SR_ERR_NA;
		*data = g_variant_new_boolean(state->auto_record_length);
		break;
	case SR_CONF_RANDOM_SAMPLING:
		/* Only supported on the HMO2524 and HMO3000 series. */
		if (!model->random_sampling || !model->num_random_sampling)
			return SR_ERR_NA;
		if (!(*model->scpi_dialect)[SCPI_CMD_GET_RANDOM_SAMPLING])
			return SR_ERR_NA;
		*data = g_variant_new_string((*model->random_sampling)[state->random_sampling]);
		break;
        case SR_CONF_ACQUISITION_MODE:
		/* Only supported on the HMO and RTC100x series. */
		if (!model->acquisition_mode || !model->num_acquisition_mode)
			return SR_ERR_NA;
		if (!(*model->scpi_dialect)[SCPI_CMD_GET_ACQUISITION_MODE])
			return SR_ERR_NA;
		*data = g_variant_new_string((*model->acquisition_mode)[state->acquisition_mode]);
		break;
	case SR_CONF_INTERPOLATION_MODE:
		if (!model->interpolation_mode || !model->num_interpolation_mode)
			return SR_ERR_NA;
		*data = g_variant_new_string((*model->interpolation_mode)[state->interpolation_mode]);
		break;
	case SR_CONF_ANALOG_THRESHOLD_CUSTOM:
		/* Not available on all models. */
		if (!(*model->scpi_dialect)[SCPI_CMD_GET_ANALOG_THRESHOLD])
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if ((idx = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_double(state->analog_channels[idx].user_threshold);
		break;
	case SR_CONF_LOGIC_THRESHOLD:
		if (!model->logic_threshold || !model->num_logic_threshold)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_DIGITAL)
			return SR_ERR_NA;
		if ((idx = std_cg_idx(cg, devc->digital_groups, model->digital_pods)) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_string((*model->logic_threshold)[state->digital_pods[idx].threshold]);
		break;
	case SR_CONF_LOGIC_THRESHOLD_CUSTOM:
		if (!model->logic_threshold || !model->num_logic_threshold)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_DIGITAL)
			return SR_ERR_NA;
		if ((idx = std_cg_idx(cg, devc->digital_groups, model->digital_pods)) < 0)
			return SR_ERR_ARG;
		/* Check if the oscilloscope is currently in custom threshold mode. */
		for (i = 0; i < model->num_logic_threshold; i++) {
			if (!strcmp("USER2", (*model->logic_threshold)[i]))
				if (strcmp("USER2", (*model->logic_threshold)[state->digital_pods[idx].threshold]))
					return SR_ERR_NA;
			if (!strcmp("USER", (*model->logic_threshold)[i]))
				if (strcmp("USER", (*model->logic_threshold)[state->digital_pods[idx].threshold]))
					return SR_ERR_NA;
			if (!strcmp("MAN", (*model->logic_threshold)[i]))
				if (strcmp("MAN", (*model->logic_threshold)[state->digital_pods[idx].threshold]))
					return SR_ERR_NA;
		}
		*data = g_variant_new_double(state->digital_pods[idx].user_threshold);
		break;
	case SR_CONF_BANDWIDTH_LIMIT:
		if (!model->bandwidth_limit || !model->num_bandwidth_limit)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if ((idx = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_string((*model->bandwidth_limit)[state->analog_channels[idx].bandwidth_limit]);
		break;
	case SR_CONF_FFT_WINDOW:
		if (!model->fft_window_types || !model->num_fft_window_types)
			return SR_ERR_NA;
		*data = g_variant_new_string((*model->fft_window_types)[state->fft_window_type]);
		break;
	case SR_CONF_FFT_FREQUENCY_START:
		*data = g_variant_new_double(state->fft_freq_start);
		break;
	case SR_CONF_FFT_FREQUENCY_STOP:
		*data = g_variant_new_double(state->fft_freq_stop);
		break;
	case SR_CONF_FFT_FREQUENCY_SPAN:
		*data = g_variant_new_double(state->fft_freq_span);
		break;
	case SR_CONF_FFT_FREQUENCY_CENTER:
		*data = g_variant_new_double(state->fft_freq_center);
		break;
	case SR_CONF_FFT_RESOLUTION_BW:
		*data = g_variant_new_double(state->fft_rbw);
		break;
	case SR_CONF_FFT_SPAN_RBW_COUPLING:
		*data = g_variant_new_boolean(state->fft_span_rbw_coupling);
		break;
	case SR_CONF_FFT_SPAN_RBW_RATIO:
		*data = g_variant_new_uint64(state->fft_span_rbw_ratio);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret, cg_type, idx, i, j;
	unsigned int custom_threshold_idx;
	unsigned int tmp_uint;
	char command[MAX_COMMAND_SIZE], command2[MAX_COMMAND_SIZE];
	char command3[MAX_COMMAND_SIZE], command4[MAX_COMMAND_SIZE];
	char float_str[MAX_COMMAND_SIZE], *tmp_str;
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;
	double tmp_d, tmp_d2;
	gboolean update_sample_rate, need_user_index, tmp_bool;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	model = devc->model_config;
	if (!model)
		return SR_ERR_ARG;

	state = devc->model_state;
	update_sample_rate = FALSE;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->samples_limit = g_variant_get_uint64(data);
		ret = SR_OK;
		break;
	case SR_CONF_LIMIT_FRAMES:
		devc->frame_limit = g_variant_get_uint64(data);
		ret = SR_OK;
		break;
	case SR_CONF_VSCALE:
		if (!model->vscale || !model->num_vscale)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((idx = std_u64_tuple_idx(data, *model->vscale, model->num_vscale)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		g_ascii_formatd(float_str, sizeof(float_str), "%E",
			(float) (*model->vscale)[idx][0] / (*model->vscale)[idx][1]);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_VERTICAL_SCALE],
			   j + 1, float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->analog_channels[j].vscale = idx;
		ret = SR_OK;
		break;
	case SR_CONF_TIMEBASE:
		if (!model->timebases || !model->num_timebases)
			return SR_ERR_NA;
		if ((idx = std_u64_tuple_idx(data, *model->timebases, model->num_timebases)) < 0)
			return SR_ERR_ARG;
		g_ascii_formatd(float_str, sizeof(float_str), "%E",
			(float) (*model->timebases)[idx][0] / (*model->timebases)[idx][1]);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_TIMEBASE],
			   float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->timebase = idx;
		ret = SR_OK;
		update_sample_rate = TRUE;
		break;
	case SR_CONF_SAMPLERATE:
		/* Only configurable on the RTO200x. */
		if (!(*model->scpi_dialect)[SCPI_CMD_SET_SAMPLE_RATE])
			return SR_ERR_NA;
		tmp_d = g_variant_get_double(data);
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_SAMPLE_RATE],
			   float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->sample_rate = tmp_d;
		ret = SR_OK;
		break;
        case SR_CONF_WAVEFORM_SAMPLE_RATE:
		/* Not supported on all models. */
		if (!model->waveform_sample_rate || !model->num_waveform_sample_rate)
			return SR_ERR_NA;
		if (!(*model->scpi_dialect)[SCPI_CMD_SET_WAVEFORM_SAMPLE_RATE])
			return SR_ERR_NA;
		if ((idx = std_str_idx(data, *model->waveform_sample_rate, model->num_waveform_sample_rate)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_WAVEFORM_SAMPLE_RATE],
			   (*model->waveform_sample_rate)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->waveform_sample_rate = idx;
		ret = SR_OK;
		break;
	case SR_CONF_AUTO_RECORD_LENGTH:
		/* Only supported on the RTB2000, RTM3000 and RTA4000. */
		if (!(*model->scpi_dialect)[SCPI_CMD_SET_AUTO_RECORD_LENGTH])
			return SR_ERR_NA;
		tmp_bool = g_variant_get_boolean(data);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_AUTO_RECORD_LENGTH],
			   tmp_bool ? "ON" : "OFF");
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->auto_record_length = tmp_bool;
		ret = SR_OK;
		break;
	case SR_CONF_RANDOM_SAMPLING:
		/* Only supported on the HMO2524 and HMO3000 series. */
		if (!model->random_sampling || !model->num_random_sampling)
			return SR_ERR_NA;
		if (!(*model->scpi_dialect)[SCPI_CMD_SET_RANDOM_SAMPLING])
			return SR_ERR_NA;
		if ((idx = std_str_idx(data, *model->random_sampling, model->num_random_sampling)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_RANDOM_SAMPLING],
			   (*model->random_sampling)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->random_sampling = idx;
		ret = SR_OK;
		break;
        case SR_CONF_ACQUISITION_MODE:
		/* Only supported on the HMO and RTC100x series. */
		if (!model->acquisition_mode || !model->num_acquisition_mode)
			return SR_ERR_NA;
		if (!(*model->scpi_dialect)[SCPI_CMD_SET_ACQUISITION_MODE])
			return SR_ERR_NA;
		if ((idx = std_str_idx(data, *model->acquisition_mode, model->num_acquisition_mode)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_ACQUISITION_MODE],
			   (*model->acquisition_mode)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->acquisition_mode = idx;
		ret = SR_OK;
		break;
	case SR_CONF_INTERPOLATION_MODE:
		if (!model->interpolation_mode || !model->num_interpolation_mode)
			return SR_ERR_NA;
		if ((idx = std_str_idx(data, *model->interpolation_mode, model->num_interpolation_mode)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_INTERPOLATION_MODE],
			   (*model->interpolation_mode)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->interpolation_mode = idx;
		ret = SR_OK;
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		tmp_d = g_variant_get_double(data);
		if (tmp_d < 0.0 || tmp_d > 1.0)
			return SR_ERR;
		if (!model->timebases || !model->num_timebases)
			return SR_ERR_NA;
		tmp_d2 = -(tmp_d - 0.5) *
			((double) (*model->timebases)[state->timebase][0] /
			(*model->timebases)[state->timebase][1])
			 * model->num_xdivs;
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d2);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_HORIZ_TRIGGERPOS],
			   float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->horiz_triggerpos = tmp_d;
		ret = SR_OK;
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!model->trigger_sources || !model->num_trigger_sources)
			return SR_ERR_NA;
		if ((idx = std_str_idx(data, *model->trigger_sources, model->num_trigger_sources)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_TRIGGER_SOURCE],
			   (*model->trigger_sources)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->trigger_source = idx;
		ret = SR_OK;
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (!model->trigger_slopes || !model->num_trigger_slopes)
			return SR_ERR_NA;
		if ((idx = std_str_idx(data, *model->trigger_slopes, model->num_trigger_slopes)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_TRIGGER_SLOPE],
			   (*model->trigger_slopes)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->trigger_slope = idx;
		ret = SR_OK;
		break;
	case SR_CONF_TRIGGER_PATTERN:
		tmp_str = (char *)g_variant_get_string(data, (gsize *)&idx);
		if (idx <= 0 || idx > MAX_TRIGGER_PATTERN_LENGTH)
			return SR_ERR_ARG;
		if (strncmp("RTO", sdi->model, 3)) {
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_TRIGGER_PATTERN],
				   tmp_str);
			if (sr_scpi_send(sdi->conn, command) != SR_OK ||
			    sr_scpi_get_opc(sdi->conn) != SR_OK)
				return SR_ERR;
		} else {
			/* RTO200x: Only available on digital channels. */
			if (idx > DIGITAL_CHANNELS_PER_POD * model->digital_pods)
				return SR_ERR_ARG;
			for (i = 0; i < idx; i++) {
				if (!strncmp("0", &tmp_str[i], 1)) {
					g_snprintf(command, sizeof(command),
						   (*model->scpi_dialect)[SCPI_CMD_SET_TRIGGER_PATTERN],
						   i, "LOW");
				} else if (!strncmp("1", &tmp_str[i], 1)) {
					g_snprintf(command, sizeof(command),
						   (*model->scpi_dialect)[SCPI_CMD_SET_TRIGGER_PATTERN],
						   i, "HIGH");
				} else {
					g_snprintf(command, sizeof(command),
						   (*model->scpi_dialect)[SCPI_CMD_SET_TRIGGER_PATTERN],
						   i, "DONT");
				}
				if (sr_scpi_send(sdi->conn, command) != SR_OK ||
				    sr_scpi_get_opc(sdi->conn) != SR_OK)
					return SR_ERR;
			}
		}
		strncpy(state->trigger_pattern, tmp_str, idx);
		ret = SR_OK;
		break;
	case SR_CONF_HIGH_RESOLUTION:
		/* Not currently implemented on RTO200x. */
		if (!(*model->scpi_dialect)[SCPI_CMD_SET_HIGH_RESOLUTION] ||
		    !(*model->scpi_dialect)[SCPI_CMD_SET_PEAK_DETECTION])
			return SR_ERR_NA;
		tmp_bool = g_variant_get_boolean(data);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_HIGH_RESOLUTION],
			   tmp_bool ? "AUTO" : "OFF");
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		/* High Resolution mode automatically switches off Peak Detection. */
		if (tmp_bool) {
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_PEAK_DETECTION],
				   "OFF");
			if (sr_scpi_send(sdi->conn, command) != SR_OK ||
					 sr_scpi_get_opc(sdi->conn) != SR_OK)
				return SR_ERR;
			state->peak_detection = FALSE;
		}
		state->high_resolution = tmp_bool;
		ret = SR_OK;
		break;
	case SR_CONF_PEAK_DETECTION:
		/* Not currently implemented on RTO200x. */
		if (!(*model->scpi_dialect)[SCPI_CMD_SET_PEAK_DETECTION] ||
		    !(*model->scpi_dialect)[SCPI_CMD_SET_HIGH_RESOLUTION])
			return SR_ERR_NA;
		tmp_bool = g_variant_get_boolean(data);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_PEAK_DETECTION],
			   tmp_bool ? "AUTO" : "OFF");
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		/* Peak Detection automatically switches off High Resolution mode. */
		if (tmp_bool) {
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_HIGH_RESOLUTION],
				   "OFF");
			if (sr_scpi_send(sdi->conn, command) != SR_OK ||
					 sr_scpi_get_opc(sdi->conn) != SR_OK)
				return SR_ERR;
			state->high_resolution = FALSE;
		}
		state->peak_detection = tmp_bool;
		ret = SR_OK;
		break;
	case SR_CONF_COUPLING:
		if (!model->coupling_options || !model->num_coupling_options)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((idx = std_str_idx(data, *model->coupling_options, model->num_coupling_options)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_COUPLING],
			   j + 1, (*model->coupling_options)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->analog_channels[j].coupling = idx;
		ret = SR_OK;
		break;
	case SR_CONF_ANALOG_THRESHOLD_CUSTOM:
		/* Not available on all models. */
		if (!(*model->scpi_dialect)[SCPI_CMD_SET_ANALOG_THRESHOLD])
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		tmp_d = g_variant_get_double(data);
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_ANALOG_THRESHOLD],
			   j + 1, float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->analog_channels[j].user_threshold = tmp_d;
		ret = SR_OK;
		break;
	case SR_CONF_LOGIC_THRESHOLD:
		if (!model->logic_threshold || !model->num_logic_threshold)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_DIGITAL)
			return SR_ERR_NA;
		if ((idx = std_str_idx(data, *model->logic_threshold, model->num_logic_threshold)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->digital_groups, model->digital_pods)) < 0)
			return SR_ERR_ARG;
                /* Check if the threshold command is based on the POD or nibble channel index. */
		if (model->logic_threshold_for_pod)
			i = j + 1;
		else
			i = j * DIGITAL_CHANNELS_PER_POD + 1;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_THRESHOLD],
			   i, (*model->logic_threshold)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		/* Same as above, but for the second nibble (second channel), if needed. */
		if (!model->logic_threshold_for_pod) {
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_THRESHOLD],
				   (j + 1) * DIGITAL_CHANNELS_PER_POD - DIGITAL_CHANNELS_PER_NIBBLE + 1,
				   (*model->logic_threshold)[idx]);
			if (sr_scpi_send(sdi->conn, command) != SR_OK ||
			    sr_scpi_get_opc(sdi->conn) != SR_OK)
				return SR_ERR;
		}
		state->digital_pods[j].threshold = idx;
		ret = SR_OK;
		break;
	case SR_CONF_LOGIC_THRESHOLD_CUSTOM:
		if (!model->logic_threshold || !model->num_logic_threshold)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_DIGITAL)
			return SR_ERR_NA;
		if ((j = std_cg_idx(cg, devc->digital_groups, model->digital_pods)) < 0)
			return SR_ERR_ARG;
		tmp_d = g_variant_get_double(data);
		/* The RTO200x has an extended range of allowed values. */
		if (strncmp("RTO", sdi->model, 3)) {
			if (tmp_d < -2.0 || tmp_d > 8.0)
				return SR_ERR;
		} else {
			if (tmp_d < -8.0 || tmp_d > 8.0)
				return SR_ERR;
		}
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d);
		/* Check if the threshold command is based on the POD or nibble channel index. */
		if (model->logic_threshold_for_pod)
			idx = j + 1;
		else
			idx = j * DIGITAL_CHANNELS_PER_POD + 1;
		/* Try to support different dialects exhaustively. */
		custom_threshold_idx = model->num_logic_threshold;
		need_user_index = FALSE;
		for (i = 0; i < model->num_logic_threshold; i++) {
			if (!strcmp("USER2", (*model->logic_threshold)[i]))
				need_user_index = TRUE;
			if (!strcmp("USER2", (*model->logic_threshold)[i]) ||
			    !strcmp("USER", (*model->logic_threshold)[i]) ||
			    !strcmp("MAN", (*model->logic_threshold)[i])) {
				custom_threshold_idx = i;
				break;
			}
		}
		/* If the dialect is supported, build the SCPI command strings and send them. */
		if (custom_threshold_idx < model->num_logic_threshold) {
			if (need_user_index) {
				g_snprintf(command, sizeof(command),
					   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD],
					   idx, 2, float_str); /* USER2 */
			} else {
				if (strncmp("RTO", sdi->model, 3)) {
					g_snprintf(command, sizeof(command),
						   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD],
						   idx, float_str);
				} else { /* The RTO200x divides each POD in two channel groups. */
					g_snprintf(command, sizeof(command),
						   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD],
						   idx, idx * 2 - 1, float_str);
					if (sr_scpi_send(sdi->conn, command) != SR_OK ||
					    sr_scpi_get_opc(sdi->conn) != SR_OK)
						return SR_ERR;
					g_snprintf(command, sizeof(command),
						   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD],
						   idx, idx * 2, float_str);
				}
			}

			if (sr_scpi_send(sdi->conn, command) != SR_OK ||
			    sr_scpi_get_opc(sdi->conn) != SR_OK)
				return SR_ERR;

			g_snprintf(command2, sizeof(command2),
				   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_THRESHOLD],
				   idx, (*model->logic_threshold)[custom_threshold_idx]);

			if (sr_scpi_send(sdi->conn, command2) != SR_OK ||
			    sr_scpi_get_opc(sdi->conn) != SR_OK)
				return SR_ERR;

			/* Set the same custom threshold on the second nibble, if needed. */
			if (!model->logic_threshold_for_pod) {
				if (need_user_index) {
					g_snprintf(command3, sizeof(command3),
						   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD],
						   (j + 1) * DIGITAL_CHANNELS_PER_POD - DIGITAL_CHANNELS_PER_NIBBLE + 1,
						   2, float_str); /* USER2 */
				} else {
					g_snprintf(command3, sizeof(command3),
						   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD],
						   (j + 1) * DIGITAL_CHANNELS_PER_POD - DIGITAL_CHANNELS_PER_NIBBLE + 1,
						   float_str);
				}

				if (sr_scpi_send(sdi->conn, command3) != SR_OK ||
				    sr_scpi_get_opc(sdi->conn) != SR_OK)
					return SR_ERR;

				g_snprintf(command4, sizeof(command4),
					   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_THRESHOLD],
					   (j + 1) * DIGITAL_CHANNELS_PER_POD - DIGITAL_CHANNELS_PER_NIBBLE + 1,
					   (*model->logic_threshold)[custom_threshold_idx]);

				if (sr_scpi_send(sdi->conn, command4) != SR_OK ||
				    sr_scpi_get_opc(sdi->conn) != SR_OK)
					return SR_ERR;
			}

			state->digital_pods[j].user_threshold = tmp_d;
			ret = SR_OK;
		}
		break;
	case SR_CONF_BANDWIDTH_LIMIT:
		if (!model->bandwidth_limit || !model->num_bandwidth_limit)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if ((idx = std_str_idx(data, *model->bandwidth_limit, model->num_bandwidth_limit)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_BANDWIDTH_LIMIT],
			   j + 1, (*model->bandwidth_limit)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->analog_channels[j].bandwidth_limit = idx;
		ret = SR_OK;
		break;
	case SR_CONF_FFT_WINDOW:
		if ((idx = std_str_idx(data, *model->fft_window_types, model->num_fft_window_types)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_FFT_WINDOW_TYPE],
			   MATH_WAVEFORM_INDEX, (*model->fft_window_types)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->fft_window_type = idx;
		ret = SR_OK;
		break;
	case SR_CONF_FFT_FREQUENCY_START:
		tmp_d = g_variant_get_double(data);
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_FFT_FREQUENCY_START],
			   MATH_WAVEFORM_INDEX, float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->fft_freq_start = tmp_d;
		ret = SR_OK;
		break;  
	case SR_CONF_FFT_FREQUENCY_STOP:
		tmp_d = g_variant_get_double(data);
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_FFT_FREQUENCY_STOP],
			   MATH_WAVEFORM_INDEX, float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->fft_freq_stop = tmp_d;
		ret = SR_OK;
		break;
	case SR_CONF_FFT_FREQUENCY_SPAN:
		tmp_d = g_variant_get_double(data);
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_FFT_FREQUENCY_SPAN],
			   MATH_WAVEFORM_INDEX, float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->fft_freq_span = tmp_d;
		ret = SR_OK;
		break;
	case SR_CONF_FFT_FREQUENCY_CENTER:
		tmp_d = g_variant_get_double(data);
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_FFT_FREQUENCY_CENTER],
			   MATH_WAVEFORM_INDEX, float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->fft_freq_center = tmp_d;
		ret = SR_OK;
		break;
	case SR_CONF_FFT_RESOLUTION_BW:
		tmp_d = g_variant_get_double(data);
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_FFT_RESOLUTION_BW],
			   MATH_WAVEFORM_INDEX, float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->fft_rbw = tmp_d;
		ret = SR_OK;
		break;
	case SR_CONF_FFT_SPAN_RBW_COUPLING:
		tmp_bool = g_variant_get_boolean(data);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_FFT_SPAN_RBW_COUPLING],
			   MATH_WAVEFORM_INDEX, tmp_bool ? "ON" : "OFF");
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->fft_span_rbw_coupling = tmp_bool;
		ret = SR_OK;
		break;
	case SR_CONF_FFT_SPAN_RBW_RATIO:
		tmp_uint = g_variant_get_uint64(data);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_FFT_SPAN_RBW_RATIO],
			   MATH_WAVEFORM_INDEX, tmp_uint);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->fft_span_rbw_ratio = tmp_uint;
		ret = SR_OK;
		break;
	default:
		ret = SR_ERR_NA;
		break;
	}

	if (ret == SR_OK && update_sample_rate)
		ret = rs_update_sample_rate(sdi);

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int cg_type = CG_NONE;
	const struct dev_context *devc = NULL;
	const struct scope_config *model = NULL;

	if (sdi) {
		devc = sdi->priv;
		if (!devc)
			return SR_ERR_ARG;

		if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
			return SR_ERR;

		model = devc->model_config;
		if (!model)
			return SR_ERR_ARG;
	}

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = std_gvar_array_u32(ARRAY_AND_SIZE(scanopts));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		if (!cg) {
			if (model)
				*data = std_gvar_array_u32(*model->devopts, model->num_devopts);
			else
				*data = std_gvar_array_u32(ARRAY_AND_SIZE(drvopts));
		} else if (cg_type == CG_ANALOG) {
			if (!model)
				return SR_ERR_ARG;
			*data = std_gvar_array_u32(*model->devopts_cg_analog, model->num_devopts_cg_analog);
		} else if (cg_type == CG_DIGITAL) {
			if (!model)
				return SR_ERR_ARG;
			*data = std_gvar_array_u32(*model->devopts_cg_digital, model->num_devopts_cg_digital);
		} else {
			*data = std_gvar_array_u32(NULL, 0);
		}
		break;
	case SR_CONF_COUPLING:
		if (!model)
			return SR_ERR_ARG;
		if (!model->coupling_options || !model->num_coupling_options)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		*data = g_variant_new_strv(*model->coupling_options, model->num_coupling_options);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!model)
			return SR_ERR_ARG;
		if (!model->trigger_sources || !model->num_trigger_sources)
			return SR_ERR_NA;
		*data = g_variant_new_strv(*model->trigger_sources, model->num_trigger_sources);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (!model)
			return SR_ERR_ARG;
		if (!model->trigger_slopes || !model->num_trigger_slopes)
			return SR_ERR_NA;
		*data = g_variant_new_strv(*model->trigger_slopes, model->num_trigger_slopes);
		break;
	case SR_CONF_TIMEBASE:
		if (!model)
			return SR_ERR_ARG;
		if (!model->timebases || !model->num_timebases)
			return SR_ERR_NA;
		*data = std_gvar_tuple_array(*model->timebases, model->num_timebases);
		break;
	case SR_CONF_WAVEFORM_SAMPLE_RATE:
		if (!model)
			return SR_ERR_ARG;
		/* Make sure it is supported by the specific model. */
		if (!model->waveform_sample_rate || !model->num_waveform_sample_rate)
			return SR_ERR_NA;
		*data = g_variant_new_strv(*model->waveform_sample_rate, model->num_waveform_sample_rate);
		break;
	case SR_CONF_RANDOM_SAMPLING:
		if (!model)
			return SR_ERR_ARG;
		/* Make sure it is supported by the specific model. */
		if (!model->random_sampling || !model->num_random_sampling)
			return SR_ERR_NA;
		*data = g_variant_new_strv(*model->random_sampling, model->num_random_sampling);
		break;
	case SR_CONF_ACQUISITION_MODE:
		if (!model)
			return SR_ERR_ARG;
		/* Make sure it is supported by the specific model. */
		if (!model->acquisition_mode || !model->num_acquisition_mode)
			return SR_ERR_NA;
		*data = g_variant_new_strv(*model->acquisition_mode, model->num_acquisition_mode);
		break;
	case SR_CONF_INTERPOLATION_MODE:
		if (!model)
			return SR_ERR_ARG;
		if (!model->interpolation_mode || !model->num_interpolation_mode)
			return SR_ERR_NA;
		*data = g_variant_new_strv(*model->interpolation_mode, model->num_interpolation_mode);
		break;
	case SR_CONF_VSCALE:
		if (!model)
			return SR_ERR_ARG;
		if (!model->vscale || !model->num_vscale)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		*data = std_gvar_tuple_array(*model->vscale, model->num_vscale);
		break;
	case SR_CONF_LOGIC_THRESHOLD:
		if (!model)
			return SR_ERR_ARG;
		if (!model->logic_threshold || !model->num_logic_threshold)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		*data = g_variant_new_strv(*model->logic_threshold, model->num_logic_threshold);
		break;
	case SR_CONF_BANDWIDTH_LIMIT:
		if (!model)
			return SR_ERR_ARG;
		if (!model->bandwidth_limit || !model->num_bandwidth_limit)
			return SR_ERR_NA;
		*data = g_variant_new_strv(*model->bandwidth_limit, model->num_bandwidth_limit);
		break;
	case SR_CONF_FFT_WINDOW:
		if (!model)
			return SR_ERR_ARG;
		if (!model->fft_window_types || !model->num_fft_window_types)
			return SR_ERR_NA;
		*data = g_variant_new_strv(*model->fft_window_types, model->num_fft_window_types);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

SR_PRIV int rs_request_data(const struct sr_dev_inst *sdi)
{
	char command[MAX_COMMAND_SIZE];
	char tmp_str[MAX_COMMAND_SIZE];
	struct sr_channel *ch;
	const struct dev_context *devc;
	const struct scope_state *state;
	const struct scope_config *model;

	if (!sdi)
		return SR_ERR;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR;

	state = devc->model_state;
	if (!state)
		return SR_ERR;

	model = devc->model_config;
	if (!model)
		return SR_ERR;

	ch = devc->current_channel->data;

	switch (ch->type) {
	case SR_CHANNEL_ANALOG:
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_GET_ANALOG_DATA],
#ifdef WORDS_BIGENDIAN
			   "MSBF",
#else
			   "LSBF",
#endif
			   ch->index + 1);
		break;
	case SR_CHANNEL_LOGIC:
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_GET_DIG_DATA],
			   ch->index / DIGITAL_CHANNELS_PER_POD + 1);
		break;
	case SR_CHANNEL_FFT:
		/* Math Expression is restored on dev_acquisition_stop(). */
		g_snprintf(tmp_str, sizeof(tmp_str),
			   "%s(%s)", FFT_MATH_EXPRESSION, (*model->analog_names)[ch->index]);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_MATH_EXPRESSION],
			   MATH_WAVEFORM_INDEX, tmp_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK) {
			sr_err("Failed to enable the FFT mode!");
			return SR_ERR;
		}
		/* Set the FFT sample rate. */
		g_ascii_formatd(tmp_str, sizeof(tmp_str), "%E", state->fft_sample_rate);
		if (strncmp("RTO", sdi->model, 3))
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_FFT_SAMPLE_RATE],
				   MATH_WAVEFORM_INDEX, tmp_str);
		else
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_SAMPLE_RATE],
				   tmp_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK) {
			if (strncmp("RTO", sdi->model, 3))
				sr_err("Failed to set the FFT sample rate!");
			else
				sr_err("Failed to set the sample rate!");
			return SR_ERR;
		}
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_GET_FFT_DATA],
			   MATH_WAVEFORM_INDEX,
			   MATH_WAVEFORM_INDEX,
			   MATH_WAVEFORM_INDEX,
#ifdef WORDS_BIGENDIAN
			   "MSBF",
#else
			   "LSBF",
#endif
			   MATH_WAVEFORM_INDEX);
		break;
	default:
		sr_err("Invalid channel type.");
		break;
	}

	return sr_scpi_send(sdi->conn, command);
}

static int rs_check_channels(const char *model, GSList *channels)
{
	GSList *l;
	struct sr_channel *ch;
	gboolean enabled_chan[MAX_ANALOG_CHANNEL_COUNT];
	gboolean enabled_pod[MAX_DIGITAL_GROUP_COUNT];
	size_t idx;

	/* Preset "not enabled" for all channels / pods. */
	for (idx = 0; idx < ARRAY_SIZE(enabled_chan); idx++)
		enabled_chan[idx] = FALSE;
	for (idx = 0; idx < ARRAY_SIZE(enabled_pod); idx++)
		enabled_pod[idx] = FALSE;

	/*
	 * Determine which channels / pods are required for the caller's
	 * specified configuration.
	 */
	for (l = channels; l; l = l->next) {
		ch = l->data;
		switch (ch->type) {
		case SR_CHANNEL_ANALOG:
			idx = ch->index;
			if (idx < ARRAY_SIZE(enabled_chan))
				enabled_chan[idx] = TRUE;
			break;
		case SR_CHANNEL_LOGIC:
			idx = ch->index / DIGITAL_CHANNELS_PER_POD;
			if (idx < ARRAY_SIZE(enabled_pod))
				enabled_pod[idx] = TRUE;
			break;
		case SR_CHANNEL_FFT:
			break;
		default:
			return SR_ERR;
		}
	}

	/*
	 * Check for resource conflicts. For example, on the HMO series
	 * with 4 analog channels, POD1 cannot be used together with
	 * the third analog channel and POD2 cannot be used together with
	 * the fourth analog channel.
	 *
	 * Apparently the above limitation has been removed from the newer
	 * RT series.
	 */
	if (!strncmp("HMO", model, 3)) {
		if (enabled_pod[0] && enabled_chan[2])
			return SR_ERR;
		if (enabled_pod[1] && enabled_chan[3])
			return SR_ERR;
	}

	return SR_OK;
}

static int rs_setup_channels(const struct sr_dev_inst *sdi)
{
	GSList *l;
	unsigned int i, j;
	gboolean *pod_enabled, setup_changed, fft_enabled = FALSE;
	char command[MAX_COMMAND_SIZE];
	const struct scope_state *state;
	const struct scope_config *model;
	struct sr_channel *ch;
	const struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	int ret;

	if (!sdi)
		return SR_ERR;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR;

	scpi = sdi->conn;

	state = devc->model_state;
	if (!state)
		return SR_ERR;

	model = devc->model_config;
	if (!model)
		return SR_ERR;

	setup_changed = FALSE;

	pod_enabled = g_try_malloc0(sizeof(gboolean) * model->digital_pods);
	if (!pod_enabled)
		return SR_ERR_MALLOC;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		switch (ch->type) {
		case SR_CHANNEL_FFT:
			if (ch->enabled)
				fft_enabled = TRUE;
			else
				break; /* Do not deactivate the corresponding analog channel ! */
			/* fall through */
		case SR_CHANNEL_ANALOG:
			if (ch->enabled == state->analog_channels[ch->index].state)
				break;
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_ANALOG_CHAN_STATE],
				   ch->index + 1, ch->enabled);

			if (sr_scpi_send(scpi, command) != SR_OK) {
				g_free(pod_enabled);
				return SR_ERR;
			}
			state->analog_channels[ch->index].state = ch->enabled;
			setup_changed = TRUE;
			break;
		case SR_CHANNEL_LOGIC:
			/*
			 * A digital POD needs to be enabled for every group of
			 * DIGITAL_CHANNELS_PER_POD channels.
			 */
			if (ch->enabled)
				pod_enabled[ch->index / DIGITAL_CHANNELS_PER_POD] = TRUE;

			if (ch->enabled == state->digital_channels[ch->index])
				break;
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_CHAN_STATE],
				   ch->index, ch->enabled);

			if (sr_scpi_send(scpi, command) != SR_OK) {
				g_free(pod_enabled);
				return SR_ERR;
			}

			state->digital_channels[ch->index] = ch->enabled;
			setup_changed = TRUE;
			break;
		default:
			g_free(pod_enabled);
			return SR_ERR;
		}
	}

	if (fft_enabled)
		sleep(1);

	ret = SR_OK;
	for (i = 0; i < model->digital_pods; i++) {
		if (state->digital_pods[i].state == pod_enabled[i])
			continue;
		if (strncmp("RTO", sdi->model, 3)) {
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_STATE],
				   i + 1, pod_enabled[i]);
			if (sr_scpi_send(scpi, command) != SR_OK) {
				ret = SR_ERR;
				break;
			}
		} else { /* On the RTO200x all bits in the POD need to be enabled individually. */
			for (j = 0; j < DIGITAL_CHANNELS_PER_POD; j++) {
				/* To disable a POD (bus), assign the channels to an unused bus (i.e. 3 or 4). */
				g_snprintf(command, sizeof(command),
					   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_STATE],
					   pod_enabled[i] ? i + 1 : i + 3, i * DIGITAL_CHANNELS_PER_POD + j, 1);
				if (sr_scpi_send(scpi, command) != SR_OK) {
					ret = SR_ERR;
					goto exit;
				}
			}
		}
		state->digital_pods[i].state = pod_enabled[i];
		setup_changed = TRUE;
	}

exit:
	g_free(pod_enabled);
	if (ret != SR_OK)
		return ret;

	if (setup_changed && rs_update_sample_rate(sdi) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	GSList *l;
	gboolean digital_added[MAX_DIGITAL_GROUP_COUNT];
	size_t group, pod_count;
	struct sr_channel *ch;
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *model;
	struct sr_scpi_dev_inst *scpi;
	int ret;
	gboolean fft_enabled = FALSE;
	gboolean update_sample_rate = TRUE;
	float fft_minimum_sample_rate;
	char command[MAX_COMMAND_SIZE];

	if (!sdi)
		return SR_ERR;

	scpi = sdi->conn;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR;

	state = devc->model_state;
	if (!state)
		return SR_ERR;

	model = devc->model_config;
	if (!model)
		return SR_ERR;

	devc->num_samples = 0;
	devc->num_frames = 0;

	/* Save the current waveform acquisition / sample rate setting. */
	state->restore_waveform_sample_rate = state->waveform_sample_rate;

	/* Save the current Automatic Record Length setting. */
	state->restore_auto_record_length = state->auto_record_length;

	/* Preset empty results. */
	for (group = 0; group < ARRAY_SIZE(digital_added); group++)
		digital_added[group] = FALSE;
	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	/*
	 * Contruct the list of enabled channels. Determine the highest
	 * number of digital pods involved in the acquisition.
	 */
	pod_count = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;
		/* Only add a single digital channel per group (pod). */
		group = ch->index / DIGITAL_CHANNELS_PER_POD;
		if (ch->type != SR_CHANNEL_LOGIC || !digital_added[group]) {
			devc->enabled_channels = g_slist_append(
					devc->enabled_channels, ch);
			if (ch->type == SR_CHANNEL_LOGIC) {
				digital_added[group] = TRUE;
				if (pod_count < group + 1)
					pod_count = group + 1;
			}
		}
		/* Check if the FFT has been requested. */
		if (ch->type == SR_CHANNEL_FFT) {
			fft_enabled = TRUE;
		}
	}
	if (!devc->enabled_channels)
		return SR_ERR;
	devc->pod_count = pod_count;
	devc->logic_data = NULL;

	/*
	 * Check constraints. Some channels can be either analog or
	 * digital, but not both at the same time.
	 */
	if (rs_check_channels(sdi->model, devc->enabled_channels) != SR_OK) {
		sr_err("Invalid channel configuration specified!");
		ret = SR_ERR_NA;
		goto free_enabled;
	}

	/*
	 * Configure the analog and digital channels and the
	 * corresponding digital pods.
	 */
	if (rs_setup_channels(sdi) != SR_OK) {
		sr_err("Failed to setup channel configuration!");
		ret = SR_ERR;
		goto free_enabled;
	}

	/* If the FFT has been requested, properly configure the oscilloscope
	 * and FFT sample rates. */
	if (fft_enabled) {
		fft_minimum_sample_rate = FFT_DDC_LP_FILTER_FACTOR * state->fft_freq_span;
		/* Set the maximum analog channel sample rate. Not supported on all models. */
		if (model->waveform_sample_rate && model->num_waveform_sample_rate &&
		    (*model->scpi_dialect)[SCPI_CMD_SET_WAVEFORM_SAMPLE_RATE]) {
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_WAVEFORM_SAMPLE_RATE],
				   (*model->waveform_sample_rate)[MAXIMUM_SAMPLE_RATE_INDEX]);
			if (sr_scpi_send(scpi, command) != SR_OK ||
			    sr_scpi_get_opc(scpi) != SR_OK) {
				update_sample_rate = FALSE;
				sr_err("Failed to set the Maximum Sample Rate!");
				if (state->sample_rate < fft_minimum_sample_rate) {
					sr_warn("The sample rate might be too small for the selected FFT frequency span!");
					sr_warn("Try manually setting the Maximum Sample Rate for reliable results...");
				}
			}
		}
		/* Set the Automatic Record Length (implies maximum sample rate). Not supported on all models. */
		if ((*model->scpi_dialect)[SCPI_CMD_SET_AUTO_RECORD_LENGTH]) {
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_AUTO_RECORD_LENGTH],
				   "ON");
			if (sr_scpi_send(scpi, command) != SR_OK ||
			    sr_scpi_get_opc(scpi) != SR_OK) {
				update_sample_rate = FALSE;
				sr_err("Failed to set the Automatic Record Length!");
				if (state->sample_rate < fft_minimum_sample_rate) {
					sr_warn("The sample rate might be too small for the selected FFT frequency span!");
					sr_warn("Try manually setting the Record Length to Automatic for reliable results...");
				}
			}
		}
		/* If the sample rate has been set to the maximum, read its new value. */
		if (update_sample_rate) {
			if (rs_update_sample_rate(sdi) != SR_OK) {
				sr_err("Failed to get the sample rate!");
				ret = SR_ERR;
				goto free_enabled;
			}
		}

		/*
		 * Set the FFT sample rate equal to either the maximum oscilloscope
		 * sample rate or the minimum value required by the selected FFT
		 * frequency span.
		 */
#ifdef FFT_SET_MAX_SAMPLING_RATE
		state->fft_sample_rate = state->sample_rate < fft_minimum_sample_rate ?
					fft_minimum_sample_rate : state->sample_rate;
#else
		state->fft_sample_rate = fft_minimum_sample_rate;
#endif
	}

	/*
	 * Start acquisition on the first enabled channel. The
	 * receive routine will continue driving the acquisition.
	 */
	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
			rs_receive_data, (void *)sdi);

	std_session_send_df_header(sdi);

	devc->current_channel = devc->enabled_channels;

	return rs_request_data(sdi);

free_enabled:
	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;
	return ret;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const struct scope_config *model;
	const struct scope_state *state;
	struct sr_scpi_dev_inst *scpi;
	GSList *l;
	struct sr_channel *ch;
	gboolean fft_enabled = FALSE;
	char command[MAX_COMMAND_SIZE];

	if (!sdi)
		return SR_ERR;

	std_session_send_df_end(sdi);

	devc = sdi->priv;
	if (!devc)
		return SR_ERR;

	model = devc->model_config;
	if (!model)
		return SR_ERR;

	state = devc->model_state;
	if (!state)
		return SR_ERR;

	scpi = sdi->conn;

	devc->num_samples = 0;
	devc->num_frames = 0;

	for (l = devc->enabled_channels; l; l = l->next) {
		ch = l->data;
		if (ch->type == SR_CHANNEL_FFT) {
			fft_enabled = TRUE;
			break;
		}
	}
	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	/*
	 * Restore waveform acquisition rate / sample rate setting and
	 * Math Expression after performing the FFT.
	 */
	if (fft_enabled) {
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_MATH_EXPRESSION],
			   MATH_WAVEFORM_INDEX, state->restore_math_expr);
		sr_scpi_send(scpi, command);
		/* Restore the waveform acquisition rate / sample rate. Not supported on all models. */
		if (model->waveform_sample_rate && model->num_waveform_sample_rate &&
		    (*model->scpi_dialect)[SCPI_CMD_SET_WAVEFORM_SAMPLE_RATE]) {
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_WAVEFORM_SAMPLE_RATE],
				   (*model->waveform_sample_rate)[state->restore_waveform_sample_rate]);
			sr_scpi_send(scpi, command);
		}
		/* Restore the Automatic Record Length mode. Not supported on all models. */
		if ((*model->scpi_dialect)[SCPI_CMD_SET_AUTO_RECORD_LENGTH]) {
			if (!state->restore_auto_record_length) {
				g_snprintf(command, sizeof(command),
					   (*model->scpi_dialect)[SCPI_CMD_SET_AUTO_RECORD_LENGTH],
					   "OFF");
				sr_scpi_send(scpi, command);
			}
		}
	}

	sr_scpi_source_remove(sdi->session, scpi);

	return SR_OK;
}

static struct sr_dev_driver rohde_schwarz_driver_info = {
	.name = "rohde-schwarz-hameg",
	.longname = "Rohde&Schwarz / Hameg oscilloscope",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(rohde_schwarz_driver_info);
