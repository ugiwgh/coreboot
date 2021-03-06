/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2018 Facebook Inc
 * Copyright (C) 2015-2016 Intel Corp.
 * (Written by Andrey Petrov <andrey.petrov@intel.com> for Intel Corp.)
 * (Written by Alexandru Gagniuc <alexandrux.gagniuc@intel.com> for Intel Corp.)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <security/vboot/antirollback.h>
#include <program_loading.h>
#include <security/vboot/vboot_common.h>
#include <vb2_api.h>
#include <security/tpm/tss.h>
#include <fsp/memory_init.h>
#include <console/console.h>
#include <string.h>

void mrc_cache_update_hash(const uint8_t *data, size_t size)
{
	uint8_t data_hash[VB2_SHA256_DIGEST_SIZE];
	static const uint8_t dead_hash[VB2_SHA256_DIGEST_SIZE] = {
		0xba, 0xad, 0xda, 0x1a, /* BAADDA1A */
		0xde, 0xad, 0xde, 0xad, /* DEADDEAD */
		0xde, 0xad, 0xda, 0x1a, /* DEADDA1A */
		0xba, 0xad, 0xba, 0xad, /* BAADBAAD */
		0xba, 0xad, 0xda, 0x1a, /* BAADDA1A */
		0xde, 0xad, 0xde, 0xad, /* DEADDEAD */
		0xde, 0xad, 0xda, 0x1a, /* DEADDA1A */
		0xba, 0xad, 0xba, 0xad, /* BAADBAAD */
	};
	const uint8_t *hash_ptr = data_hash;

	/* We do not store normal mode data hash in TPM. */
	if (!vboot_recovery_mode_enabled())
		return;

	/* Initialize TPM driver. */
	if (tlcl_lib_init() != VB2_SUCCESS) {
		printk(BIOS_ERR, "MRC: TPM driver initialization failed.\n");
		return;
	}

	/* Calculate hash of data generated by MRC. */
	if (vb2_digest_buffer(data, size, VB2_HASH_SHA256, data_hash,
			      sizeof(data_hash))) {
		printk(BIOS_ERR, "MRC: SHA-256 calculation failed for data. "
		       "Not updating TPM hash space.\n");
		/*
		 * Since data is being updated in recovery cache, the hash
		 * currently stored in TPM recovery hash space is no longer
		 * valid. If we are not able to calculate hash of the data being
		 * updated, reset all the bits in TPM recovery hash space to
		 * pre-defined hash pattern.
		 */
		hash_ptr = dead_hash;
	}

	/* Write hash of data to TPM space. */
	if (antirollback_write_space_rec_hash(hash_ptr, VB2_SHA256_DIGEST_SIZE)
	    != TPM_SUCCESS) {
		printk(BIOS_ERR, "MRC: Could not save hash to TPM.\n");
		return;
	}

	printk(BIOS_INFO, "MRC: TPM MRC hash updated successfully.\n");
}

int mrc_cache_verify_hash(const uint8_t *data, size_t size)
{
	uint8_t data_hash[VB2_SHA256_DIGEST_SIZE];
	uint8_t tpm_hash[VB2_SHA256_DIGEST_SIZE];

	/* We do not store normal mode data hash in TPM. */
	if (!vboot_recovery_mode_enabled())
		return 1;

	/* Calculate hash of data read from RECOVERY_MRC_CACHE. */
	if (vb2_digest_buffer(data, size, VB2_HASH_SHA256, data_hash,
			      sizeof(data_hash))) {
		printk(BIOS_ERR, "MRC: SHA-256 calculation failed for data.\n");
		return 0;
	}

	/* Initialize TPM driver. */
	if (tlcl_lib_init() != VB2_SUCCESS) {
		printk(BIOS_ERR, "MRC: TPM driver initialization failed.\n");
		return 0;
	}

	/* Read hash of MRC data saved in TPM. */
	if (antirollback_read_space_rec_hash(tpm_hash, sizeof(tpm_hash))
	    != TPM_SUCCESS) {
		printk(BIOS_ERR, "MRC: Could not read hash from TPM.\n");
		return 0;
	}

	if (memcmp(tpm_hash, data_hash, sizeof(tpm_hash))) {
		printk(BIOS_ERR, "MRC: Hash comparison failed.\n");
		return 0;
	}

	printk(BIOS_INFO, "MRC: Hash comparison successful. "
	       "Using data from RECOVERY_MRC_CACHE\n");
	return 1;
}
