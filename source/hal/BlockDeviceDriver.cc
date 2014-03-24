/*
 * BlockDeviceDriver.cc
 *
 *  Created on: 14.06.2013
 *      Author: dbaldin
 */

#include "BlockDeviceDriver.hh"
#include <kernel/Kernel.hh>

extern Kernel* theOS;
ArrayDatabase *BlockDeviceDriver::freeBlockDeviceIDs;

BlockDeviceDriver::BlockDeviceDriver(char* p_name) :  GenericDeviceDriver(cBlockDevice, true, p_name) {

	SimpleFileManager* fm = theOS->getFileManager();

	/* be sure we have a filesystem. if not we can not continue since every driver needs to register!! */
	if (fm != 0) fm->registerResource( this );

	/* conservative default */
	this->sector_size = 512;
}

BlockDeviceDriver::~BlockDeviceDriver() {
	SimpleFileManager* fm = theOS->getFileManager();

	/* be sure we have a filesystem. if not we can not continue since every driver needs to register!! */
	if (fm != 0) fm->unregisterResource( this );
}

