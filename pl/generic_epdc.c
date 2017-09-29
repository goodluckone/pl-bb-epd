/*
 * generic_epdc.c
 *
 *  Created on: 23.03.2015
 *      Author: sebastian.friebe
 */

#include <pl/assert.h>
#include <pl/generic_epdc.h>
#define LOG_TAG "generic_epdc"
#include <pl/utils.h>


static void generic_epdc_delete(struct pl_generic_epdc *p);
static int set_vcom(struct pl_generic_epdc *p, int vcomInMillivolt);
static int read_register(struct pl_generic_epdc *p, const regSetting_t* setting);
static int write_register(struct pl_generic_epdc *p, const regSetting_t setting, const uint32_t bitmask);
static int send_cmd(struct pl_generic_epdc *p, const regSetting_t setting);
static int do_clear_update(struct pl_generic_epdc *p);
static int epdc_init(struct pl_generic_epdc *p, int load_nvm_content);
static int generic_update(struct pl_generic_epdc *p, int wfID, enum pl_update_mode mode, const struct pl_area *area);

static int unpack_nvm_content(uint8_t *buffer, int bufferSize);
static int read_vcom_from_file(const char *filename, int *vcomInMillivolts);
static int switch_hvs_on(pl_hv_t *p);
static int switch_hvs_off(pl_hv_t *p);

/**
 * allocates memory to hold a pl_generic_epdc structure
 *
 * @return pointer to allocated memory
 */
struct pl_generic_epdc *generic_epdc_new(){
	struct pl_generic_epdc *p = (struct pl_generic_epdc *)malloc(sizeof(struct pl_generic_epdc));;

	p->delete = generic_epdc_delete;

	p->hv = NULL;
	p->nvm = NULL;
	p->controller = NULL;

	p->init = epdc_init;
	p->update = generic_update;
	p->set_vcom = set_vcom;
	p->read_register = read_register;
	p->write_register = write_register;
	p->send_cmd = send_cmd;

	p->clear_init = do_clear_update;

	return p;
}

// -----------------------------------------------------------------------------
// interface functions
// ------------------------------
/**
 * frees memory specified by a given pointer
 *
 * @param p pointer to the memory to be freed
 */
static void generic_epdc_delete(struct pl_generic_epdc *p){
	if (p != NULL){
		if (p->hv != NULL) p->hv->delete(p->hv);
		if (p->nvm != NULL) p->nvm->delete(p->nvm);
		if (p->controller != NULL) p->controller->delete(p->controller);

		free(p);
		p = NULL;
	}
}

/**
 * Initializes all hardware components referenced by the EPDC.
 *
 * @param p pointer to a generic epdc structure
 * @see pl_generic_epdc
 * @return int value unequal to 0 if an error occured, otherwise 0
 */
static int epdc_init(struct pl_generic_epdc *p, int load_nvm_content){
	assert(p != NULL);
	int stat = 0;
	LOG("%s", __func__);
	// initialize controller
	pl_generic_controller_t *controller = p->controller;
	assert(controller != NULL);
	if (controller->init != NULL)
		stat |= controller->init(controller, load_nvm_content);

	if (stat) return -1;

	controller->update_temp(controller);

	// initialize hv
	pl_hv_t *hv = p->hv;
	assert(hv != NULL);
	if (hv->init != NULL)
		stat |= hv->init(hv);

	if (stat) return -1;

	// load data from display NVM and apply it's settings
	if (load_nvm_content){
		if (p->nvm == NULL){
			LOG("Abort: There's no nvm defined in the EPDC.");
			return -1;
		}

		if (p->hv->vcomConfig == NULL){
			LOG("Abort: There's no vcom configuration HW defined in the EPDC.");
			return -1;
		}

		if (p->controller == NULL){
			LOG("Abort: There's no controller HW defined in the EPDC.");
			return -1;
		}

		uint8_t *buffer = NULL;
		int bufferSize;

		// read NVM content to file
		p->nvm->read_wfdata(p->nvm, &buffer, &bufferSize);
		if (bufferSize <= 0)
			return -1;

		if (p->nvm->nvm_format == NVM_FORMAT_S040){

			// unpack vcom and waveform data from NVM content
			if (unpack_nvm_content(buffer, bufferSize))
				return -1;

			int vcomInMillivolts;

			// load vcom file and send data to vcomConfig
			if (read_vcom_from_file("/tmp/vcom_from_display_nvm", &vcomInMillivolts))
				return -1;

			if (p->hv->vcomConfig->set_vcom(p->hv->vcomConfig, vcomInMillivolts))
				return -1;

			// load waveform file and send data to controller
			if (p->controller->update_temp(p->controller))
				return -1;

			if (p->controller->load_wflib(p->controller, "/tmp/waveform_from_display_nvm.bin"))
				return -1;
		}
		else if (p->nvm->nvm_format == NVM_FORMAT_S1D13541){
			LOG("NVM_FORMAT_S1D13541 does not support Wf loading from NVM");
			return -1;
		}
		else if (p->nvm->nvm_format == NVM_FORMAT_EPSON){

			FILE *fd = fopen("/tmp/dummy.generic.wbf", "wb");
			if (fd == NULL) {
				LOG("error creating binary file.");
				return -1;
			}

			int count = fwrite(buffer, sizeof(uint8_t), bufferSize, fd);
			fclose(fd);

			if (count != bufferSize){
				LOG("error during binary file write.");
				return -1;
			}

			int isPgm = 0;
			if(p->nvm->read_header(p->nvm, &isPgm))
				return -1;

			if (p->hv->vcomConfig->set_vcom(p->hv->vcomConfig, p->nvm->vcom))
				return -1;

			// load waveform file and send data to controller
			if (p->controller->update_temp(p->controller))
				return -1;

			if (p->controller->load_wflib(p->controller, "/tmp/dummy.generic.wbf"))
				return -1;
		}
		else if (p->nvm->nvm_format == NVM_FORMAT_PLAIN){

			FILE *fd = fopen("/tmp/dummy.plain.bin", "wb");
			if (fd == NULL) {
				LOG("error creating binary file.");
				return -1;
			}

			int count = fwrite(buffer, sizeof(uint8_t), bufferSize, fd);
			fclose(fd);

			if (count != bufferSize){
				LOG("error during binary file write.");
				return -1;
			}
		}
	}
	else{
		// load waveform and vcom from std paths
		LOG("Loading wflib: %s", controller->waveform_file_path);
		if (controller->load_wflib(controller, controller->waveform_file_path))
			return -1;

		LOG("Setting vcom: %d", p->default_vcom);
		if (p->set_vcom(p, p->default_vcom))
			return -1;
	}

	return stat;

}

/**
 * Sets given target vcom in the internally referenced vcom_config hardware part
 *
 * @param p pointer to a generic epdc structure
 * @param vcomInMillivolt target vcom value in millivolt
 * @return success indicator: 0 if passed, otherwise <> 0
 */
static int set_vcom(struct pl_generic_epdc *p, int vcomInMillivolt){
	struct pl_vcom_config *vcom_config = p->hv->vcomConfig;

	assert(vcom_config != NULL);
	return vcom_config->set_vcom(vcom_config, vcomInMillivolt);
}

/**
 * Reads register values from internally referenced epd controller and
 * stores the results in the register settings given as second parameter.
 *
 * @param p pointer to a generic epdc structure
 * @param setting register settings
 * @return success indicator: 0 if passed, otherwise <> 0
 */
static int read_register(struct pl_generic_epdc *p, const regSetting_t* setting){
	assert(p != NULL);

	pl_generic_controller_t *controller = p->controller;
	assert(controller != NULL);

	return controller->read_register(controller, setting);
}

/**
 * Writes given register values to internally referenced epd controller.
 *
 * @param p pointer to a generic epdc structure
 * @param setting register settings (addresses, values)
 * @param bitmask bitmask for write operation, write only bits which are '1' in the mask
 * @return success indicator: 0 if passed, otherwise <> 0
 */
static int write_register(struct pl_generic_epdc *p, const regSetting_t setting, const uint32_t bitmask){
	assert(p != NULL);

	pl_generic_controller_t *controller = p->controller;
	assert(controller != NULL);

	return controller->write_register(controller, setting, bitmask);
}

/**
 * Sends a command with given arguments to the internally referenced epd controller.
 *
 * @param p pointer to a generic epdc structure
 * @param setting command settings (cmd, arguments)
 * @return success indicator: 0 if passed, otherwise <> 0
 */
static int send_cmd(struct pl_generic_epdc *p, const regSetting_t setting){
	assert(p != NULL);

	pl_generic_controller_t *controller = p->controller;
	assert(controller != NULL);

	return controller->send_cmd(controller, setting);
}

/**
 * Executes a complete update sequence on the epdc.
 *
 * @param p pointer to a generic epdc structure
 * @param wfid waveform id used for the update
 * @param mode refers to the update mode, i.e. full update, partial update
 * @param area definition of an update area (can be NULL)
 * @return success indicator: 0 if passed, otherwise <> 0
 */
static int generic_update(struct pl_generic_epdc *p, int wfID, enum pl_update_mode mode,
		const struct pl_area *area){
	assert(p != NULL);
	pl_generic_controller_t *controller = p->controller;
	pl_hv_t *hv = p->hv;
	//struct timespec t;
	//start_stopwatch(&t);
	assert(controller != NULL);
	assert(hv != NULL);

	int nowait = 0;
	if(mode > 3){
		mode -= 4;
		nowait = 1;
	}

	int stat = 0;
	//stat |= controller->wait_update_end(controller);
	if (controller->temp_mode != PL_EPDC_TEMP_MANUAL){
		if (controller->update_temp != NULL)
			stat |= controller->update_temp(controller);
	}
	//read_stopwatch(&t,"update_temp",1);
	stat |= controller->configure_update(controller, wfID, mode, area);
	//read_stopwatch(&t,"configure_update",1);
	stat |= switch_hvs_on(hv);
	//read_stopwatch(&t,"switch_hvs_on",1);
	stat |= controller->trigger_update(controller);
	//read_stopwatch(&t,"trigger_update",1);
	if(!nowait){
		stat |= controller->wait_update_end(controller);
		//read_stopwatch(&t,"cwait_update_end",1);
		stat |= switch_hvs_off(hv);
		//read_stopwatch(&t,"switch_hvs_off",1);
		}
	return stat;
}


/**
 * Executes initial clear update.
 *
 * @param p pointer to a generic epdc structure.
 * @return success indicator: 0 if passed, otherwise <> 0.
 */
static int do_clear_update(struct pl_generic_epdc *p){
	assert(p != NULL);
	assert(p->controller != NULL);

	pl_generic_controller_t *controller = p->controller;
	pl_hv_t *hv = p->hv;

	int stat = 0;

	if (p->controller->clear_update == NULL){
		LOG("Warning - clear update not supported...");
		return 0;
	}

	if (controller->temp_mode != PL_EPDC_TEMP_MANUAL){
		if (controller->update_temp != NULL)
			stat |= controller->update_temp(controller);
	}


	if (p->controller->fill(p->controller, NULL, 0xFF))
		return -1;

	stat |= switch_hvs_on(hv);

	stat |= controller->clear_update(controller);
	stat |= controller->wait_update_end(controller);

	stat |= switch_hvs_off(hv);

	return stat;
}


// -----------------------------------------------------------------------------
// private functions
// ------------------------------
/**
 * unpack content of the display nvm data. used for S040 display with i2c flash only (NVM_FORMAT_S040).
 *
 * @param buffer pointer to the packed data.
 * @param bufferSize byte size of the packed data.
 * @return status.
 */
static int unpack_nvm_content(uint8_t *buffer, int bufferSize){

	char command[200];
	const char *filename = "/tmp/dummy.nvm";

	FILE *fd = fopen(filename, "w");
	int n = fwrite(buffer, sizeof(uint8_t), bufferSize, fd);
	if (n != bufferSize){
		if (ferror(fd) != 0)
			LOG("Error (%d) during file write.", ferror(fd));
		else
			LOG("Not all bytes have been written to file.");

		return -1;
	}
	fclose(fd);

	// generate binary blob
	sprintf(command, "/home/root/scripts/extract_display_nvm_content.py %s", filename);
	if (system(command)){
		return -1;
	}

	return 0;
}

/**
 * used to read the vcom back from file after unpacking the display nvm data.
 *
 * @param path to the vcom file.
 * @param pointer to the vcom value.
 * @return status.
 */
static int read_vcom_from_file(const char *filename, int *vcomInMillivolts){

	FILE *fd = fopen(filename, "r");

	// check if the file exists
	if (fd == NULL){
		LOG("Given vcom-file (%s) not found.", filename);
		return -1;
	}

	int bytecount = fscanf(fd, "%d", vcomInMillivolts);
	fclose(fd);

	if (bytecount <= 0){
		LOG("Vcom file (%s) seems to be empty.", filename);
		return -1;
	}

	return 0;
}

/**
 * switches the high voltages on.
 *
 * @param pl_hv structure.
 * @return status.
 */
static int switch_hvs_on(pl_hv_t *hv){
	int stat = 0;

	if ((hv->hvDriver != NULL) && (hv->hvDriver->switch_on != NULL))
		stat |= hv->hvDriver->switch_on(hv->hvDriver);

	if ((hv->vcomDriver != NULL) && (hv->vcomDriver->switch_on != NULL))
		stat |= hv->vcomDriver->switch_on(hv->vcomDriver);

	if ((hv->vcomSwitch != NULL) && (hv->vcomSwitch->close != NULL) && (!hv->vcomSwitch->is_bypass))
		hv->vcomSwitch->close(hv->vcomSwitch);

	return stat;
}

/**
 * switches the high voltages off.
 *
 * @param pl_hv structure.
 * @return status.
 */
static int switch_hvs_off(pl_hv_t *hv){
	int stat = 0;

	if ((hv->vcomSwitch != NULL) && (hv->vcomSwitch->open != NULL) && (!hv->vcomSwitch->is_bypass))
		hv->vcomSwitch->open(hv->vcomSwitch);

	if ((hv->vcomDriver != NULL) && (hv->vcomDriver->switch_off != NULL))
		stat |= hv->vcomDriver->switch_off(hv->vcomDriver);

	if ((hv->hvDriver != NULL) && (hv->hvDriver->switch_off != NULL))
		stat |= hv->hvDriver->switch_off(hv->hvDriver);

	return stat;
}