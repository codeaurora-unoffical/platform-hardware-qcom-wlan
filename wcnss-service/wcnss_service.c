/*--------------------------------------------------------------------------
Copyright (c) 2013, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of The Linux Foundation nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#define LOG_TAG "wcnss_service"
#include <cutils/log.h>
#ifdef WCNSS_QMI
#include <dirent.h>
#include <grp.h>
#include <utime.h>
#include <cutils/properties.h>
#include "wcnss_qmi_client.h"
#endif

#define SUCCESS 0
#define FAILED -1

#define WCNSS_CAL_CHUNK (3*1024)
#define WCNSS_CAL_FILE  "/data/misc/wifi/WCNSS_qcom_wlan_cal.bin"
#define WCNSS_FACT_FILE "/data/misc/wifi/WCN_FACTORY"
#define WCNSS_DEVICE    "/dev/wcnss_wlan"

#ifdef WCNSS_QMI
#define MAX_FILE_LENGTH 1024
#define WLAN_ADDR_SIZE   6
unsigned char wlan_nv_mac_addr[WLAN_ADDR_SIZE];
#define MAC_ADDR_ARRAY(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MAC_ADDRESS_STR "%02x:%02x:%02x:%02x:%02x:%02x"

/* As Want to write in 00:0a:f5:11:22:33 fortmat in sysfs file
   so taking mac length as 12 char + 5 for ":" + NULL
 */
#define WLAN_MAC_ADDR_STRING 18

void find_full_path(char *cur_dir, char *file_to_find, char *full_path)
{
	DIR *dir;
	struct stat st;
	struct dirent *dr;
	char cwd[1024];
	int rc;

	chdir(cur_dir);

	dir = opendir(".");

	while ((dr = readdir(dir))) {

		rc = lstat(dr->d_name, &st);
		if (rc < 0) {
			ALOGE("lstat failed %s", strerror(errno));
			return;
		}
		if (S_ISDIR(st.st_mode)) {
			if ((strcmp(dr->d_name, ".")) &&
					(strcmp(dr->d_name, ".."))) {
				find_full_path(dr->d_name,
						file_to_find, full_path);
			}
		} else if (!strcmp(file_to_find, dr->d_name)) {
			getcwd(cwd, sizeof(cwd));
			snprintf(full_path, MAX_FILE_LENGTH, "%s/%s",
					cwd, file_to_find);
		}
	}
	closedir(dir);

	chdir("..");
}

void setup_wcnss_macaddress()
{
	char fpath[MAX_FILE_LENGTH];
	char mac_addr[WLAN_MAC_ADDR_STRING];
	char *pstr;
	int fd;

	find_full_path("/sys/devices", "wcnss_mac_addr", fpath);

	pstr = strstr(fpath, "wcnss");
	if (pstr == NULL) {
		ALOGE("Failed to find wcnss_mac_addr node");
		return;
	}

	ALOGE("Mac Address node %s", fpath);
	snprintf(mac_addr, WLAN_MAC_ADDR_STRING,
		MAC_ADDRESS_STR,
		MAC_ADDR_ARRAY(wlan_nv_mac_addr));

	fd = open(fpath, O_WRONLY);
	if (fd < 0) {
		ALOGE("Failed to open %s : %s", fpath, strerror(errno));
		return;
	}

	if (write(fd, mac_addr, WLAN_MAC_ADDR_STRING) < 0) {
		ALOGE("Failed to write to %s : %s", fpath, strerror(errno));
		close(fd);
		return;
	}
	close(fd);
}
#endif

int wcnss_write_cal_data(int fd_dev)
{
	int rcount = 0;
	int size = 0;
	int rc = 0;
	int wcount = 0;
	int fd_file;
	struct stat st;

	char buf[WCNSS_CAL_CHUNK];

	ALOGI("wcnss_write_cal_data trying to write cal");

	rc = stat(WCNSS_CAL_FILE, &st);
	if (rc < 0) {
		ALOGE("Failed to stat cal file : %s",
				strerror(errno));
		goto exit;
	}

	size = st.st_size;

	fd_file = open(WCNSS_CAL_FILE, O_RDONLY);
	if (fd_file < 0) {
		ALOGE("cal file doesn't exist: %s",
				strerror(errno));
		rc = fd_file;
		goto exit;
	}

	/* write the file size first, so that platform driver knows
	 * when it recieves the full data */
	wcount = write(fd_dev, (void *)&size, 4);
	if (wcount != 4) {
		ALOGE("Failed to write to wcnss device : %s",
				strerror(errno));
		rc = wcount;
		goto exit_close;
	}

	do {
		rcount = read(fd_file, (void *)buf, sizeof(buf));
		if (rcount < 0) {
			ALOGE("Failed to read from cal file ; %s",
					strerror(errno));
			rc = rcount;
			goto exit_remove;
		}

		if (!rcount)
			break;

		wcount = write(fd_dev, buf, rcount);
		if (wcount < 0) {
			ALOGE("Failed to write to wcnss device : %s",
				strerror(errno));
			rc = wcount;
			goto exit_close;
		}

	} while (rcount);
	close(fd_file);

	return SUCCESS;

exit_remove:
	close(fd_file);
	remove("WCNSS_CAL_FILE");
	return rc;

exit_close:
	close(fd_file);

exit:
	return rc;
}

int wcnss_read_and_store_cal_data(int fd_dev)
{
	int rcount = 0;
	int wcount = 0;
	int fd_file = -1;
	int rc = 0;

	char buf[WCNSS_CAL_CHUNK];

	ALOGI("wcnss_read_and_store_cal_data trying to read cal");

	do {
		/* wait on this read until data comes from fw */
		rcount = read(fd_dev, (void *)buf, sizeof(buf));
		if (rcount < 0) {
			ALOGE("Failed to read from wcnss device : %s",
					strerror(errno));
			rc = rcount;
			goto exit;
		}

		/* truncate the file only if there is fw data, this read
		 * may never return if the fw decides that no more cal is
		 * required; and the data we have now is good enough.
		 */
		if (fd_file < 0) {
			fd_file = open(WCNSS_CAL_FILE, O_WRONLY
					| O_CREAT | O_TRUNC, 0664);
			if (fd_file < 0) {
				ALOGE("Failed to open cal file : %s",
						strerror(errno));
				rc = fd_file;
				goto exit;
			}
		}

		if (!rcount)
			break;

		wcount = write(fd_file, buf, rcount);
		if (wcount < 0) {
			ALOGE("Failed to write to cal file : %s",
				strerror(errno));
			rc = wcount;
			goto exit_remove;
		}

	} while (rcount);

	close(fd_file);

	return SUCCESS;

exit_remove:
	close(fd_file);
	remove(WCNSS_CAL_FILE);

exit:
	return rc;
}

int main(int argc, char *argv[])
{
	int rc;
	int fd_dev;
	struct stat st;

#ifdef WCNSS_QMI
	/* initialize the DMS client and request the wlan mac address */

	if (SUCCESS == wcnss_init_qmi()) {

		rc = wcnss_qmi_get_wlan_address(wlan_nv_mac_addr);

		if (rc == SUCCESS) {
			ALOGE("WLAN MAC Addr:" MAC_ADDRESS_STR,
				MAC_ADDR_ARRAY(wlan_nv_mac_addr));
			setup_wcnss_macaddress();
		} else
			ALOGE("Failed to Get MAC addr from modem");

		wcnss_qmi_deinit();
	}
	else
		ALOGE("Failed to Initialize wcnss QMI Interface");
#endif

	fd_dev = open(WCNSS_DEVICE, O_RDWR);
	if (fd_dev < 0) {
		ALOGE("Failed to open wcnss device : %s",
				strerror(errno));
		return fd_dev;
	}

	rc = stat(WCNSS_FACT_FILE, &st);
	if (rc == 0) {
		ALOGE("Factory file found, deleting cal file");
		unlink(WCNSS_CAL_FILE);
	} else {
		rc = wcnss_write_cal_data(fd_dev);
		if (rc != SUCCESS)
			ALOGE("No cal data is written to WCNSS %d", rc);
		else
			ALOGE("Cal data is successfully written to WCNSS");
	}

	rc = wcnss_read_and_store_cal_data(fd_dev);
	if (rc != SUCCESS)
		ALOGE("Failed to read and save cal data %d", rc);
	else
		ALOGI("Calibration data was successfull written to %s",
			WCNSS_CAL_FILE);

	close(fd_dev);

	return rc;
}
