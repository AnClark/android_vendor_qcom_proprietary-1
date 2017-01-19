/*
 * Copyright (c) 2011-2013 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 *
 * Collect peripheral subsystem ramdumps and logs and output to SD card or eMMC
 *
 * Support internal subsystem ramdumps.
 * Support external subsystem ramdumps including MDM and QSC.
 * Support RPM logging.
 * Support QDSS ramdumps
 */

#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/netlink.h>

#include <cutils/log.h>
#include <dirent.h>

#define BUFFER_SIZE 0x00008000

#define LOG_NUM 1
#define DUMP_NUM 10

#define FILENAME_SIZE 60
#define PIL_NAME_SIZE 30
#define TIMEBUF_SIZE 21
#define UEVENT_BUF_SIZE 1024
#define RPMLOG_BUF_SIZE 0x00500000
#define MAX_STR_LEN 36
#define CHK_ELF_LEN 5

/* External modem ramdump */
#define UEVENT_MDM_SHUTDOWN "remove@/devices/platform/diag_bridge"
#define UEVENT_MDM_POWERUP "add@/devices/platform/diag_bridge"
#define EXT_MDM_DIR "/tombstones/mdm"
#define EXT_MDM2_DIR "/tombstones/mdm2"
#define STR_MDM "/mdm"
#define STR_QSC "/qsc"
#define MDM_CHECKER "/dev/mdm"
#define QSC_CHECKER "/data/misc/mdmhelperdata/qschelpsocket"
#define PIL_RAMDUMP_DIR "/dev"

/* Generic subsystem ramdump */
#define DUMP_SETTING "/sys/module/subsystem_restart/parameters/enable_ramdumps"
#define DUMP_SDCARD_DIR "/sdcard/ramdump"
#define DUMP_EMMC_DIR "/data/ramdump"
#define DUMP_SMEM_STR "ramdump_smem"
#define DUMP_MODEM_STR "ramdump_modem"
#define DUMP_MODEM_SW_STR "ramdump_modem_sw"
#define DUMP_AUDIO_OCMEM_STR "ramdump_audio-ocmem"
#define DUMP_HEAD_STR "ramdump_"
#define DUMP_TAIL_BIN ".bin"
#define DUMP_TAIL_ELF ".elf"
#define STR_ELF "ELF"

/* QDSS ramdump */
#define QDSS_ETF_BUFSIZE 0x00010000
#define QDSS_ETF_CUR_SINK "/sys/bus/coresight/devices/coresight-tmc-etf/curr_sink"
#define QDSS_ETF_DUMP "coresight-tmc-etf"
#define QDSS_ETR_BUFSIZE 0x00100000
#define QDSS_ETR_CUR_SINK "/sys/bus/coresight/devices/coresight-tmc-etr/curr_sink"
#define QDSS_ETR_OUT_MODE "/sys/bus/coresight/devices/coresight-tmc-etr/out_mode"
#define QDSS_ETR_DUMP "coresight-tmc-etr"

/* SSR events */
#define SSR_EVENT_MDM_RAMDUMP 0x00000001
#define SSR_EVENT_QSC_RAMDUMP 0x00000002
#define SSR_EVENT_GET_RPM_LOG 0x00000004
#define SSR_EVENT_MDM_RESTART 0x00000008
#define SSR_EVENT_QSC_RESTART 0x00000010

typedef struct {
	int fd[DUMP_NUM];
	int dev[DUMP_NUM];
	char *dir;
} ramdump_s;

char ramdump_list[DUMP_NUM][PIL_NAME_SIZE];

/* List of log dev */
char *LOG_LIST[LOG_NUM] = {
	"/sys/kernel/debug/rpm_log"
};

/* Log output files */
char *LOG_NODES[LOG_NUM] = {
	"/rpm_log"
};

static ramdump_s ramdump;

/* Poll struct */
static struct pollfd pfd[DUMP_NUM];
static struct pollfd plogfd[LOG_NUM];

static sem_t ramdump_sem;
static sem_t qdss_sem;
static unsigned int ssr_flag;
static unsigned int rpm_log;
static unsigned int qdss_dump;
static int pfd_num;

/*==========================================================================*/
/* Local Function declarations */
/*==========================================================================*/
int generate_ramdump(int index, ramdump_s *dump, char *tm);
static int parse_args(int argc, char **argv);

/*===========================================================================*/
int check_folder(char *f_name)
{
	int ret = 0;
	struct stat st;

	if ((ret = stat(f_name, &st)) != 0)
		fprintf(stderr, "Directory %s does not exist\n", f_name);

	return ret;
}

int create_folder(char *f_name)
{
	int ret = 0;

	if ((ret = mkdir(f_name, S_IRWXU | S_IRWXG | S_IRWXO)) < 0)
		fprintf(stderr, "Unable to create %s\n", f_name);

	return ret;
}

char *get_current_timestamp(char *buf, int len)
{
	time_t local_time;
	struct tm *tm;

	if (buf == NULL || len < TIMEBUF_SIZE) {
		fprintf(stderr, "Invalid timestamp buffer\n");
		goto get_timestamp_error;
	}

	/* Get current time */
	local_time = time(NULL);
	if (!local_time) {
		fprintf(stderr, "Unable to get timestamp\n");
		goto get_timestamp_error;
	}

	tm = localtime(&local_time);
	if (!tm) {
		fprintf(stderr, "Unable to get local time\n");
		goto get_timestamp_error;
	}

	snprintf(buf, TIMEBUF_SIZE,
		"_%04d-%02d-%02d_%02d-%02d-%02d", tm->tm_year+1900,
		tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min,
		tm->tm_sec);

	return buf;

get_timestamp_error:
	return NULL;
}

void ssr_ramdump_signal_handler(int sig)
{
	switch (sig) {
	case SIGUSR1:
	case SIGUSR2:
	case SIGTERM:
		/* call thread exits */
		pthread_exit(NULL);
		break;

	default:
		break;
	}
}

int ssr_stop_thread(pthread_t thread_id, int sig)
{
	int ret = 0;

	/* Signal the thread to exit */
	ret = pthread_kill(thread_id, sig);
	if (ret != 0) {
		fprintf(stderr, "Unable to terminate thread %lu\n", thread_id);
	} else {
		if ((ret = pthread_join(thread_id, NULL)) != 0) {
			fprintf(stderr, "pthread_join failed for thread %lu\n",
								thread_id);
		}
	}

	return ret;
}

int assemble_name_string(char *out, int len, char *head, char *tail)
{
	/* check buf and len */
	if (out == NULL || head == NULL || tail == NULL || len <= 0)
		return -EINVAL;

	if (out != head)
		strlcpy(out, head, len);
	strlcat(out, tail, len);

	return 0;
}

int copy_file(char *out_f, char *in_f)
{
	int ret = 0;
	int in, out;
	int rbytes;
	char buf[BUFFER_SIZE];

	if (out_f == NULL || in_f == NULL) {
		ret = -EINVAL;
		goto skip_copy;
	}

	/* check if source file exist */
	if (access(in_f, F_OK) != 0) {
		ret = -EINVAL;
		goto skip_copy;
	}

	/* make sure output file doesn't exist before creation */
	remove(out_f);

	in = open(in_f, O_RDONLY);
	if (in < 0) {
		fprintf(stderr, "Unable to open %s\n", in_f);
		ret = -EIO;
		goto skip_copy;
	}

	out = open(out_f, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	if (out < 0) {
		fprintf(stderr, "Unable to open %s\n", out_f);
		ret = -EIO;
		goto open_error;
	}

	while ((rbytes = read(in, buf, BUFFER_SIZE)) > 0) {
		ret = write(out, buf, rbytes);
		if (ret < 0) {
			fprintf(stderr, "Unable to write to file %s\n", out_f);
			break;
		}
	}
	close(out);

open_error:
	close(in);

skip_copy:
	return ret;
}

static int ssr_copy_dump(char *dst, char *src, char *tm)
{
	int ret = 0;
	char dir[FILENAME_SIZE];
	char out_file[FILENAME_SIZE];
	char in_file[FILENAME_SIZE];
	struct dirent *dir_p;
	DIR *dir_s;

	assemble_name_string(dir, FILENAME_SIZE, ramdump.dir, dst);
	assemble_name_string(dir, FILENAME_SIZE, dir, tm);
	if (create_folder(dir) < 0) {
		fprintf(stderr, "Unable to create directory %s\n", dir);
		ret = -EINVAL;
		goto skip_copy;
	}

	dir_s = opendir(src);
	if (!dir_s) {
		fprintf(stderr, "Unable to open %s\n", src);
		ret = -EINVAL;
		goto skip_copy;
	}
	while ((dir_p = readdir(dir_s)) != NULL) {
		if ((strncmp(dir_p->d_name, ".", 1) != 0) &&
				(strncmp(dir_p->d_name, "..", 2) != 0)) {
			strlcpy(out_file, dir, FILENAME_SIZE);
			strlcat(out_file, "/", FILENAME_SIZE);
			strlcat(out_file, dir_p->d_name, FILENAME_SIZE);

			strlcpy(in_file, src, FILENAME_SIZE);
			strlcat(in_file, "/", FILENAME_SIZE);
			strlcat(in_file, dir_p->d_name, FILENAME_SIZE);

			if (copy_file(out_file, in_file) < 0) {
				ret = -EINVAL;
				break;
			}
		}
	}
	closedir(dir_s);

	if (ret < 0)
		remove(dir);

skip_copy:
	return ret;
}

static void *ext_modem_mon(void* param)
{
	int ret;
	int type = 0;
	char timestamp[TIMEBUF_SIZE];
	struct stat st;

	while (1) {
		/* wait external modem ramdump ready */
		sem_wait(&ramdump_sem);

		/*
		* External modem ramdump should be collected after a completed
		* restart. Don't collect legacy ramdumps
		*/
		if (!(ssr_flag & SSR_EVENT_MDM_RESTART) &&
					!(ssr_flag & SSR_EVENT_QSC_RESTART))
			continue;
		ssr_flag &= ~(SSR_EVENT_MDM_RESTART | SSR_EVENT_QSC_RESTART);

		/* get current time */
		if (get_current_timestamp(timestamp, TIMEBUF_SIZE) == NULL) {
			fprintf(stderr, "Unable to get timestamp\n");
			break;
		}

		/* check ramdump source location */
		if (stat(EXT_MDM_DIR, &st) != 0) {
			fprintf(stderr, "Directory %s does not exist\n",
								EXT_MDM_DIR);
			break;
		}

		if (ssr_flag & SSR_EVENT_MDM_RAMDUMP)
			type |= 0x1;

		if (ssr_flag & SSR_EVENT_QSC_RAMDUMP)
			type |= 0x2;

		if (type == 1) {
			/* MDM ramdump only */
			ret = ssr_copy_dump(STR_MDM, EXT_MDM_DIR, timestamp);
			if (ret != 0)
				fprintf(stderr, "MDM ramdump failed\n");
			else
				fprintf(stderr, "MDM ramdump completed\n");
		} else if (type == 2) {
			/* QSC ramdump only */
			ret = ssr_copy_dump(STR_QSC, EXT_MDM_DIR, timestamp);
			if (ret != 0)
				fprintf(stderr, "QSC ramdump failed\n");
			else
				fprintf(stderr, "QSC ramdump completed\n");
		} else {
			/* MDM and QSC ramdump */
			if (stat(EXT_MDM2_DIR, &st) != 0) {
				fprintf(stderr, "Directory %s does not exist\n",
								EXT_MDM2_DIR);
				break;
			}

			ret = ssr_copy_dump(STR_MDM, EXT_MDM_DIR, timestamp);
			if (ret != 0)
				fprintf(stderr, "MDM ramdump failed\n");
			else
				fprintf(stderr, "MDM ramdump completed\n");

			ret = ssr_copy_dump(STR_QSC, EXT_MDM2_DIR, timestamp);
			if (ret != 0)
				fprintf(stderr, "QSC ramdump failed\n");
			else
				fprintf(stderr, "QSC ramdump completed\n");
		}
		type = 0;
	}

	return NULL;
}

int open_uevent()
{
	struct sockaddr_nl addr;
	int sz = UEVENT_BUF_SIZE;
	int s;

	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = 0xffffffff;

	/*
	*	netlink(7) on nl_pid:
	*	If the application sets it to 0, the kernel takes care of
	*	assigning it.
	*	The kernel assigns the process ID to the first netlink socket
	*	the process opens and assigns a unique nl_pid to every netlink
	*	socket that the process subsequently creates.
	*/
	addr.nl_pid = getpid();

	s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if(s < 0) {
		fprintf(stderr, "Creating netlink socket failed\n");
		return -1;
	}

	setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, &sz, sizeof(sz));

	if(bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		fprintf(stderr, "Binding to netlink socket failed\n");
		close(s);
		return -1;
	}

	return s;
}

int uevent_next_event(int fd, char* buffer, int buffer_length)
{
	struct pollfd fds;
	int nr;
	int count;

	while (1) {
		fds.fd = fd;
		fds.events = POLLIN;
		fds.revents = 0;
		nr = poll(&fds, 1, -1);

		if (nr > 0 && fds.revents == POLLIN) {
			count = recv(fd, buffer, buffer_length, 0);
			if (count > 0)
				return count;
			fprintf(stderr, "Receiving uevent failed\n");
		}
	}

	return 0;
}

static void *uevent_mon(void* param)
{
	int i;
	int ufd;
	int count;
	char uevent_buf[UEVENT_BUF_SIZE];

	/* open uevent fd */
	ufd = open_uevent();
	if (ufd < 0) {
		fprintf(stderr, "Failed to initialize uevent\n");
		return NULL;
	}

	while (1) {
		/* Listen for user space event */
		count = uevent_next_event(ufd, uevent_buf, UEVENT_BUF_SIZE);
		if (!count)
			break;

		/*
		* Look for a completed MDM restart
		* MDM power down event: set restart flag
		* MDM power up event: post to semaphore
		*/
		for (i = 0; i < MAX_STR_LEN; i++) {
			if (*(uevent_buf + i) == '\0')
				break;
		}
		if (i == MAX_STR_LEN)
			*(uevent_buf + i) = '\0';

		if (strstr(uevent_buf, UEVENT_MDM_SHUTDOWN)) {
			ssr_flag |= SSR_EVENT_MDM_RESTART;
			if (rpm_log)
				ssr_flag |= SSR_EVENT_GET_RPM_LOG;
		} else if (strstr(uevent_buf, UEVENT_MDM_POWERUP)) {
			sem_post(&ramdump_sem);
		}
	}

	close(ufd);

	return NULL;
}

int generate_log(char *buf, int len)
{
	int ret = 0;
	int fd;
	char name_buf[FILENAME_SIZE];
	char timestamp[TIMEBUF_SIZE];

	/* get current time */
	if (get_current_timestamp(timestamp, TIMEBUF_SIZE) == NULL) {
		fprintf(stderr, "Unable to get timestamp\n");
		ret = -EINVAL;
		goto timestamp_error;
	}

	/* Assemble output file */
	assemble_name_string(name_buf, FILENAME_SIZE, ramdump.dir,
							LOG_NODES[0]);
	assemble_name_string(name_buf, FILENAME_SIZE, name_buf,
							timestamp);
	assemble_name_string(name_buf, FILENAME_SIZE, name_buf,
							DUMP_TAIL_BIN);

	/* Open the output file */
	fd = open(name_buf, O_WRONLY | O_CREAT, S_IRUSR);
	if (fd < 0) {
		fprintf(stderr, "Unable to open log file %s\n", name_buf);
		ret = -EIO;
		goto open_error;
	}

	/* write to file */
	ret = write(fd, buf, len);
	if (ret < 0) {
		fprintf(stderr, "Unable to write to log file %s\n", name_buf);
		goto write_error;
	}

	/* Make sure things are written */
	fsync(fd);

write_error:
	close(fd);

open_error:
timestamp_error:
	return ret;
}

static void *rpm_log_thread(void* param)
{
	int ret;
	int count = 0;
	char *read_buf;
	char *rpm_log_buf;
	int rpm_log_len = 0;

	rpm_log_buf = (char *)malloc(RPMLOG_BUF_SIZE);
	if (rpm_log_buf == NULL) {
		fprintf(stderr, "Failed to allocate memory to collect rpm log\n");
		return NULL;
	}

	while (1) {
		/* Poll RPM log */
		if ((ret = poll(plogfd, LOG_NUM, -1)) < 0) {
			fprintf(stderr, "Polling rpm log fd failed: %d\n", ret);
			break;
		}

		read_buf = malloc(BUFFER_SIZE);
		if (read_buf == NULL) {
			fprintf(stderr, "Failed to allocate memory to read rpm log\n");
			break;
		}

		/* Collect RPM log */
		count = read(plogfd[0].fd, read_buf, BUFFER_SIZE);

		if ((rpm_log_len + count) >= RPMLOG_BUF_SIZE)
			count = RPMLOG_BUF_SIZE - rpm_log_len;
		if (count) {
			memcpy(rpm_log_buf + rpm_log_len, read_buf, count);
			rpm_log_len += count;
		}

		/* Store rpm log while subystem crash */
		if (ssr_flag & SSR_EVENT_GET_RPM_LOG) {
			ssr_flag &= ~SSR_EVENT_GET_RPM_LOG;
			if (generate_log(rpm_log_buf, rpm_log_len) < 0) {
				fprintf(stderr, "Failed to generate rpm log\n");
				free(read_buf);
				break;
			}
			rpm_log_len = 0;
		}

		if (rpm_log_len >= RPMLOG_BUF_SIZE)
			rpm_log_len = 0;

		free(read_buf);
	}
	free(rpm_log_buf);

	return NULL;
}

int log_init(void)
{
	int ret = 0;

	/* check if log device exist */
	if (access(LOG_LIST[0], F_OK) != 0) {
		ret = -EIO;
		goto open_error;
	}

	plogfd[0].fd = open(LOG_LIST[0], O_RDONLY);
	if (plogfd[0].fd < 0) {
		fprintf(stderr, "Unable to open %s\n", LOG_LIST[0]);
		ret = -EIO;
		goto open_error;
	}
	plogfd[0].events = POLLIN;
	plogfd[0].revents = 0;

open_error:
	return ret;
}

static void ssr_pil_ramdump_notification(int id)
{
	/* RPM log collection only needs to do once for each SSR. Skip
	 * duplicating SSR notification
	 */
	if ((strncmp(ramdump_list[id], DUMP_SMEM_STR, 12) == 0) ||
		(strncmp(ramdump_list[id], DUMP_AUDIO_OCMEM_STR, 19) == 0) ||
		(strncmp(ramdump_list[id], DUMP_MODEM_SW_STR, 16) == 0))
		return;

	/* Collect QSC ramdump for modem ssr */
	if (strncmp(ramdump_list[id], DUMP_MODEM_STR, 13) == 0) {
		if (ssr_flag & SSR_EVENT_QSC_RAMDUMP) {
			ssr_flag |= SSR_EVENT_QSC_RESTART;
			sem_post(&ramdump_sem);
		}
	}

	/* Collect RPM log */
	if (rpm_log)
		ssr_flag |= SSR_EVENT_GET_RPM_LOG;

	/* Collect qdss dump */
	if (qdss_dump)
		sem_post(&qdss_sem);
}

char *ssr_ramdump_filename(int index, ramdump_s *dump, char *name, char *tm, int type)
{
	strlcpy(name, dump->dir, FILENAME_SIZE);
	strlcat(name, "/", FILENAME_SIZE);
	strlcat(name, ramdump_list[dump->dev[index]], FILENAME_SIZE);
	strlcat(name, tm, FILENAME_SIZE);
	if (type)
		strlcat(name, DUMP_TAIL_ELF, FILENAME_SIZE);
	else
		strlcat(name, DUMP_TAIL_BIN, FILENAME_SIZE);

	return name;
}

int open_ramdump_fd(ramdump_s *dump)
{
	int ret = 0;
	int i;
	int fd;
	char buf[FILENAME_SIZE];

	for (i = 0; i < pfd_num; i++) {
		strlcpy(buf, PIL_RAMDUMP_DIR, FILENAME_SIZE);
		strlcat(buf, "/", FILENAME_SIZE);
		strlcat(buf, ramdump_list[i], FILENAME_SIZE);

		if ((fd = open(buf, O_RDONLY)) < 0) {
			fprintf(stderr, "Unable to open %s\n", buf);
			ret = -EINVAL;
			break;
		}

		/* store open fd */
		dump->fd[i] = fd;
		dump->dev[i] = i;
	}

	if (ret < 0) {
		for (i = 0; i < pfd_num; i++) {
			if (dump->fd[i])
				close(dump->fd[i]);
		}
	}

	return ret;
}

static int ssr_check_ext_modem(char *chk_str)
{
	int ret = 0;

	if (access(chk_str, F_OK) != 0)
		ret = -EINVAL;

	return ret;
}

static int ssr_check_ramdump_setting(void)
{
	int ret = 0;
	int fd;
	char buf[2];

	if ((fd = open(DUMP_SETTING, O_RDONLY)) < 0) {
		fprintf(stderr, "Unable to open %s\n", DUMP_SETTING);
		ret = fd;
		goto open_error;
	}

	if (read(fd, buf, 2) <= 0) {
		fprintf(stderr, "Unable to read %s\n", DUMP_SETTING);
		ret = -EIO;
		goto read_error;
	}

	if (buf[0] == '1') {
		ret = 0;
	} else if (buf[0] == '0') {
		ret = -EINVAL;
	} else {
		fprintf(stderr, "Invalid ramdump setting %s\n", buf);
		ret = -EINVAL;
	}

read_error:
	close(fd);

open_error:
	return ret;
}

static int ssr_ramdump_init(ramdump_s *dump)
{
	int ret = 0;
	int i;

	/* check if ramdump folder can be created */
	if (check_folder(dump->dir) != 0) {
		fprintf(stderr, "Attemping to create %s\n", dump->dir);
		if ((ret = create_folder(ramdump.dir)) < 0) {
			fprintf(stderr, "Unable to create %s\n", dump->dir);
			goto open_error;
		}
	}

	/* init ramdump semaphore */
	if (sem_init(&ramdump_sem, 0, 0) != 0) {
		ret = -EINVAL;
		goto ramdump_semaphore_error;
	}

	/* init qdss semaphore */
	if (sem_init(&qdss_sem, 0, 0) != 0) {
		ret = -EINVAL;
		goto qdss_semaphore_error;
	}

	/* init ramdump parameters */
	for (i = 0; i < DUMP_NUM; i++) {
		dump->fd[i] = -1;
		dump->dev[i] = -1;
	}

	/* open fd in ramdump list */
	if ((ret = open_ramdump_fd(dump)) < 0) {
		fprintf(stderr, "Failed to open ramdump devices\n");
		goto open_ramdump_error;
	}

	/* Store fd to polling struct */
	for (i = 0; i < pfd_num; i++) {
		pfd[i].fd = dump->fd[i];
		pfd[i].events = POLLIN;
		pfd[i].revents = 0;
	}
	ssr_flag = 0;

	goto exit;

open_ramdump_error:
	sem_destroy(&qdss_sem);

qdss_semaphore_error:
	sem_destroy(&ramdump_sem);

ramdump_semaphore_error:
open_error:
	dump->dir = NULL;

exit:
	return ret;
}

static int ssr_search_pil_ramdump(void)
{
	int ret = 0;
	int i = 0;
	struct dirent *dir_p;
	DIR *dir_s;

	dir_s = opendir(PIL_RAMDUMP_DIR);
	if (!dir_s) {
		fprintf(stderr, "Unable to open %s\n", PIL_RAMDUMP_DIR);
		ret = -EINVAL;
		goto open_error;
	}

	while ((dir_p = readdir(dir_s)) != NULL) {
		if (strncmp(dir_p->d_name, DUMP_HEAD_STR, 8) == 0) {
			if (i >= DUMP_NUM) {
				fprintf(stderr, "Exceed max pil number\n");
				pfd_num = 0;
				ret = -EINVAL;
				goto invalid_num;
			}
			strlcpy(ramdump_list[i], dir_p->d_name, PIL_NAME_SIZE);
			i++;
		}
	}

	/* Store pil ramdump number */
	pfd_num = i;

invalid_num:
	closedir(dir_s);

open_error:
	return ret;
}

static int ssr_ramdump_cleanup(int num)
{
	int i;

	for (i = 0; i < num; i++)
		close(pfd[i].fd);
	sem_destroy(&ramdump_sem);
	sem_destroy(&qdss_sem);
	ramdump.dir = NULL;

	return 0;
}

static int ssr_qdss_dump(char *dev, int sz)
{
	int ret = 0;
	int qdss_fd, file_fd;
	int rd_bytes = 0;
	int sum_bytes = 0;
	char *rd_buf;
	char qdss_name[FILENAME_SIZE];
	char file_name[FILENAME_SIZE];
	char timestamp[TIMEBUF_SIZE];

	if ((dev == NULL) || (sz <= 0)) {
		fprintf(stderr, "Invalid qdss device or size\n");
		ret = -EINVAL;
		goto skip_copy;
	}

	if ((rd_buf = malloc(sz)) == NULL) {
		fprintf(stderr, "Failed to allocate memory to read qdss\n");
		ret = -ENOMEM;
		goto skip_copy;
	}

	/* Open qdss etf/etr dev */
	strlcpy(qdss_name, PIL_RAMDUMP_DIR, FILENAME_SIZE);
	strlcat(qdss_name, "/", FILENAME_SIZE);
	strlcat(qdss_name, dev, FILENAME_SIZE);
	if ((qdss_fd = open(qdss_name, O_RDONLY)) < 0) {
		fprintf(stderr, "Unable to open %s\n", qdss_name);
		ret = qdss_fd;
		goto open_error;
	}

	/* Get current time */
	if (get_current_timestamp(timestamp, TIMEBUF_SIZE) == NULL) {
		fprintf(stderr, "Unable to get timestamp\n");
		ret = -EINVAL;
		goto timestamp_error;
	}

	/* Open output file name */
	strlcpy(file_name, ramdump.dir, FILENAME_SIZE);
	strlcat(file_name, "/", FILENAME_SIZE);
	strlcat(file_name, dev, FILENAME_SIZE);
	strlcat(file_name, timestamp, FILENAME_SIZE);
	strlcat(file_name, DUMP_TAIL_BIN, FILENAME_SIZE);
	if ((file_fd = open(file_name, O_WRONLY | O_CREAT, S_IRUSR)) < 0) {
		fprintf(stderr, "Unable to open %s\n", file_name);
		ret = file_fd;
		goto file_error;
	}

	/* Read QDSS ramdump and write to stroage location */
	while ((rd_bytes = read(qdss_fd, rd_buf, BUFFER_SIZE)) > 0) {
		if (write(file_fd, rd_buf, rd_bytes) < 0) {
			fprintf(stderr, "Writing qdss ramdump error\n");
			ret = -EIO;
			goto write_error;
		}
		sum_bytes += rd_bytes;
	}

	/* Make sure things are written */
	fsync(file_fd);

	if (sum_bytes == sz) {
		fprintf(stdout, "qdss %s ramdump saved successfully\n", dev);
		ret = 0;
	} else {
		fprintf(stderr, "qdss %s ramdump size mismatch\n", dev);
		ret = -EINVAL;
	}

write_error:
	close(file_fd);

file_error:
timestamp_error:
	close(qdss_fd);

open_error:
	free(rd_buf);

skip_copy:
	return ret;
}

static int ssr_do_qdss_etf_dump(void)
{
	int ret = 0;
	int fd;
	char buf[2];

	if ((fd = open(QDSS_ETF_CUR_SINK, O_RDONLY)) < 0) {
		fprintf(stderr, "Unable to open %s\n", QDSS_ETF_CUR_SINK);
		ret = fd;
		goto open_error;
	}

	if (!read(fd, buf, 2)) {
		fprintf(stderr, "Unable to read qdss etf data\n");
		ret = -EIO;
		goto read_error;
	}

	if (buf[0] == '1')
		ret = ssr_qdss_dump(QDSS_ETF_DUMP, QDSS_ETF_BUFSIZE);

read_error:
	close(fd);

open_error:
	return ret;
}

static int ssr_do_qdss_etr_dump(void)
{
	int ret = 0;
	int sink_fd, mode_fd;
	int flag = 0;
	char sink_buf[2], mode_buf[4];

	if ((sink_fd = open(QDSS_ETR_CUR_SINK, O_RDONLY)) < 0) {
		fprintf(stderr, "Unable to open %s\n", QDSS_ETR_CUR_SINK);
		ret = sink_fd;
		goto skip_copy;
	}

	if (!read(sink_fd, sink_buf, 2)) {
		fprintf(stderr, "Unable to read qdss etr data\n");
		ret = -EIO;
		goto skip_read;
	}

	if (sink_buf[0] == '1')
		flag = 1;
	if (!flag)
		goto skip_read;

	if ((mode_fd = open(QDSS_ETR_OUT_MODE, O_RDONLY)) < 0) {
		fprintf(stderr, "Unable to open %s\n", QDSS_ETR_OUT_MODE);
		ret = mode_fd;
		goto open_error;
	}

	if (!read(mode_fd, mode_buf, 4)) {
		fprintf(stderr, "Unable to read qdss etr mode data\n");
		ret = -EIO;
		goto read_error;
	}
	if (strncmp(mode_buf, "mem", 3) != 0) {
		fprintf(stderr, "Invalid qdss etr out_mode %s\n", mode_buf);
		ret = -EINVAL;
		goto read_error;
	}

	ret = ssr_qdss_dump(QDSS_ETR_DUMP, QDSS_ETR_BUFSIZE);

read_error:
	close(mode_fd);

open_error:
skip_read:
	close(sink_fd);

skip_copy:
	return ret;
}

static void *qdss_mon(void* param)
{
	int ret;

	while (1) {
		/* wait ssr happened */
		sem_wait(&qdss_sem);

		if ((ret = ssr_do_qdss_etf_dump()) < 0) {
			fprintf(stderr, "Failed to save qdss etf dump\n");
			break;
		}

		if ((ret = ssr_do_qdss_etr_dump()) < 0) {
			fprintf(stderr, "Failed to save qdss etr dump\n");
			break;
		}
	}

	return NULL;
}

static void ssr_tool_helper(void)
{
	fprintf(stdout, "***********************************************\n");
	fprintf(stdout, "\n Qualcomm Technologies, Inc.\n");
	fprintf(stdout, " Copyright (c) 2011-2014  All Rights Reserved.\n\n");
	fprintf(stdout, " Subsystem Ramdump Apps\n");
	fprintf(stdout, " usage:\n");
	fprintf(stdout, " ./system/bin/subsystem_ramdump [arg1] [arg2] [arg3]\n");
	fprintf(stdout, " [arg1]: (1/2) Ramdump location\n");
	fprintf(stdout, " [arg2]: (1/0) Enable/disable RPM log\n\n");
	fprintf(stdout, " [arg3]: (1/0) Enable/disable qdss ramdump\n\n");
	fprintf(stdout, "***********************************************\n");
}

static int parse_args(int argc, char **argv)
{
	int ret = 0;
	int location, rpmlog, qdss;

	ssr_tool_helper();
	if (argc == 1) {
		/* Storage location */
		fprintf(stdout, "Select ramdump location:\n");
		fprintf(stdout, "1: eMMC: /data/ramdump\n");
		fprintf(stdout, "2: SD card: /sdcard/ramdump\n");
		scanf("%d", &location);

		/* RPM logging */
		fprintf(stdout, "Enable/disable RPM log:\n");
		fprintf(stdout, "1: Enable RPM log\n");
		fprintf(stdout, "0: Disable RPM log\n");
		scanf("%d", &rpmlog);

		/* qdss ramdump */
		fprintf(stdout, "Enable/disable qdss ramdump:\n");
		fprintf(stdout, "1: Enable qdss ramdump\n");
		fprintf(stdout, "0: Disable qdss ramdump\n");
		scanf("%d", &qdss);
	} else if (argc == 2) {
		/* Disable RPM log and qdss ramdump by default */
		rpmlog = 0;
		qdss = 0;
		sscanf(argv[1], "%d", &location);
	} else if (argc == 3) {
		/* Disable qdss ramdump by default */
		qdss = 0;
		sscanf(argv[1], "%d", &location);
		sscanf(argv[2], "%d", &rpmlog);
	} else if (argc == 4) {
		sscanf(argv[1], "%d", &location);
		sscanf(argv[2], "%d", &rpmlog);
		sscanf(argv[3], "%d", &qdss);
	} else {
		fprintf(stderr, "Too many input arguments\n");
		ret = -EINVAL;
		goto err_exit;
	}

	if (location == 1) {
		ramdump.dir = DUMP_EMMC_DIR;
	} else if (location == 2) {
		ramdump.dir = DUMP_SDCARD_DIR;
	} else {
		fprintf(stderr, "Invalid ramdump storage setting\n");
		ret = -EINVAL;
		goto err_exit;
	}

	if (rpmlog == 1) {
		rpm_log = 1;
	} else if (rpmlog == 0) {
		rpm_log = 0;
	} else {
		fprintf(stderr, "Invalid RPM log setting\n");
		ret = -EINVAL;
	}

	if (qdss == 1) {
		qdss_dump = 1;
	} else if (qdss == 0) {
		qdss_dump = 0;
	} else {
		fprintf(stderr, "Invalid qdss ramdump setting\n");
		ret = -EINVAL;
	}

err_exit:
	return ret;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	int i;
	char timestamp[TIMEBUF_SIZE];
	pthread_t uevent_thread_hdl;
	pthread_t ext_modem_thread_hdl;
	pthread_t rpm_log_thread_hdl;
	pthread_t qdss_thread_hdl;
	struct sigaction action;

	if ((ret = parse_args(argc, argv)) < 0) {
		fprintf(stderr, "Invalid arguments\n");
		goto invalid_args_error;
	}

	/* Exit if ramdump is not enable */
	if ((ret = ssr_check_ramdump_setting()) < 0) {
		fprintf(stderr, "Ramdump is not enable\n");
		goto invalid_setting;
	}

	/* Search PIL ramdumps */
	if ((ret = ssr_search_pil_ramdump()) < 0) {
		fprintf(stderr, "Failed to find pil ramdump\n");
		goto ramdump_search_error;
	}

	/* Register signal handlers */
	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = ssr_ramdump_signal_handler;

	if ((ret = ssr_ramdump_init(&ramdump)) < 0) {
		fprintf(stderr, "Failed to initialize ramdump\n");
		goto init_ramdump_error;
	}

	if ((ret = ssr_check_ext_modem(MDM_CHECKER)) == 0) {
		/* create thread to listen uevent */
		ret = pthread_create(&uevent_thread_hdl, NULL, uevent_mon,
									NULL);
		if (ret != 0) {
			fprintf(stderr, "Creating uevent thread failed\n");
			goto mdm_listener_error;
		}
		ssr_flag |= SSR_EVENT_MDM_RAMDUMP;
	}

	if ((ret = ssr_check_ext_modem(QSC_CHECKER)) == 0)
		ssr_flag |= SSR_EVENT_QSC_RAMDUMP;

	/* create thread to copy ext modem dump */
	if ((ssr_flag & SSR_EVENT_MDM_RAMDUMP) ||
					(ssr_flag & SSR_EVENT_QSC_RAMDUMP)) {
		ret = pthread_create(&ext_modem_thread_hdl, NULL,
							ext_modem_mon, NULL);
		if (ret != 0) {
			fprintf(stderr, "Creating MDM monitor thread failed\n");
			goto modem_dumper_error;
		}
	}

	/* create thread to collect rpm log */
	if (rpm_log) {
		if ((ret = log_init()) != 0)
			goto rpm_log_init_error;

		ret = pthread_create(&rpm_log_thread_hdl, NULL, rpm_log_thread,
									NULL);
		if (ret != 0) {
			fprintf(stderr, "Creating rpm log thread failed\n");
			goto rpm_listener_error;
		}
	}

	/* create thread to collect qdss dump */
	if (qdss_dump) {
		ret = pthread_create(&qdss_thread_hdl, NULL, qdss_mon, NULL);
		if (ret != 0) {
			fprintf(stderr, "Creating qdss monitor thread failed\n");
			goto qdss_listener_error;
		}
	}

	/* Loop forever and poll */
	while (1) {
		/* Poll all ramdump files */
		if ((ret = poll(pfd, pfd_num, -1)) < 0) {
			fprintf(stderr, "Polling ramdump failed: %d\n", ret);
			break;
		}

		/* Get current time */
		if (get_current_timestamp(timestamp, TIMEBUF_SIZE) == NULL) {
			fprintf(stderr, "Unable to get timestamp\n");
			break;
		}

		/* Collect ramdumps */
		for (i = 0; i < pfd_num; i++) {
			ret = generate_ramdump(i, &ramdump, timestamp);
			/* If ramdump generation is failed, exit the program */
			if (ret < 0) {
				fprintf(stderr, "Generating %s failed\n",
					ramdump_list[i]);
				goto ramdump_generation_error;
			}
		}
	}

ramdump_generation_error:
	if (qdss_dump)
		ret = ssr_stop_thread(qdss_thread_hdl, SIGUSR2);

qdss_listener_error:
	if (rpm_log)
		ret = ssr_stop_thread(rpm_log_thread_hdl, SIGUSR2);

rpm_listener_error:
	if (rpm_log)
		close(plogfd[0].fd);

rpm_log_init_error:
	if ((ssr_flag & SSR_EVENT_MDM_RAMDUMP) ||
					(ssr_flag & SSR_EVENT_QSC_RAMDUMP)) {
		ret = ssr_stop_thread(ext_modem_thread_hdl, SIGUSR2);
	}

modem_dumper_error:
	if (ssr_flag & SSR_EVENT_MDM_RAMDUMP)
		ret = ssr_stop_thread(uevent_thread_hdl, SIGUSR2);

mdm_listener_error:
	ssr_ramdump_cleanup(pfd_num);

init_ramdump_error:
	ssr_flag = 0;

ramdump_search_error:
invalid_setting:
invalid_args_error:
	return ret;
}

int generate_ramdump(int index, ramdump_s *dump, char *tm)
{
	int ret = 0;
	int numBytes = 0;
	int totalBytes = 0;
	int fd = -1;
	char *rd_buf;
	char f_name[FILENAME_SIZE];

	if (index < 0 || index >= DUMP_NUM) {
		ret = -EINVAL;
		goto skip_dump_generation;
	}

	/* Check to see if we have anything to do */
	if ((pfd[index].revents & POLLIN) == 0)
		goto skip_dump_generation;

	/* Notify other thread current ramdump */
	ssr_pil_ramdump_notification(dump->dev[index]);

	/* Allocate a buffer for us to use */
	if ((rd_buf = malloc(BUFFER_SIZE)) == NULL) {
		ret = -ENOMEM;
		goto skip_dump_generation;
	}

	/* Read first section of ramdump to determine type */
	while ((numBytes = read(pfd[index].fd, rd_buf, CHK_ELF_LEN)) > 0) {
		*(rd_buf + numBytes) = '\0';
		if (strstr(rd_buf, STR_ELF))
			ssr_ramdump_filename(index, dump, f_name, tm, 1);
		else
			ssr_ramdump_filename(index, dump, f_name, tm, 0);

		/* Open the output file */
		fd = open(f_name, O_WRONLY | O_CREAT, S_IRUSR);
		if (fd < 0) {
			fprintf(stderr, "Failed to open %s\n", f_name);
			ret = -EIO;
			goto open_error;
		}

		/* Write first section ramdump into file and exit loop */
		ret = write(fd, rd_buf, numBytes);
		if (ret < 0) {
			fprintf(stderr, "Failed to write to dump file %s\n",
								f_name);
			goto dump_write_error;
		}
		totalBytes += numBytes;
		break;
	}

	/* Read data from the ramdump, and write it into the output file */
	while ((numBytes = read(pfd[index].fd, rd_buf, BUFFER_SIZE)) > 0) {
		ret = write(fd, rd_buf, numBytes);
		if (ret < 0) {
			fprintf(stderr, "Failed to write to dump file %s\n",
								f_name);
			goto dump_write_error;
		}
		totalBytes += numBytes;
	}

	/* Make sure things are written */
	fsync(fd);

	/* Return the number of bytes written */
	ret = totalBytes;

dump_write_error:
	close(fd);

open_error:
	free(rd_buf);

skip_dump_generation:
	return ret;
}
