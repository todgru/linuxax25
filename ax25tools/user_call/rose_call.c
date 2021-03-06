#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <config.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netax25/ax25.h>
#include <netrose/rose.h>

#include <netax25/axlib.h>
#include <netax25/axconfig.h>
#include <netax25/rsconfig.h>

#include "user_io.h"

void alarm_handler(int sig)
{
}

int main(int argc, char **argv)
{
	char buffer[256], *addr;
	int s, addrlen = sizeof(struct sockaddr_rose);
	struct sockaddr_rose rosebind, roseconnect;

	while ((s = getopt(argc, argv, "ci:o:")) != -1) {
		switch (s) {
		case 'c':
			init_compress();
			compression = 1;
			break;
		case 'i':
			paclen_in = atoi(optarg);
			break;
		case 'o':
			paclen_out = atoi(optarg);
			break;
		case ':':
		case '?':
			err("ERROR: invalid option usage\r");
			return 1;
		}
	}

	if (paclen_in < 1 || paclen_out < 1) {
		err("ERROR: invalid paclen\r");
		return 1;
	}

	/*
	 * Arguments should be "rose_call port mycall remcall remaddr"
	 */
	if ((argc - optind) != 4) {
		strcpy(buffer, "ERROR: invalid number of parameters\r");
		err(buffer);
	}

	if (rs_config_load_ports() == 0) {
		strcpy(buffer, "ERROR: problem with rsports file\r");
		err(buffer);
	}

	/*
	 * Parse the passed values for correctness.
	 */
	roseconnect.srose_family = rosebind.srose_family = AF_ROSE;
	roseconnect.srose_ndigis = rosebind.srose_ndigis = 0;

	if ((addr = rs_config_get_addr(argv[optind])) == NULL) {
		sprintf(buffer, "ERROR: invalid Rose port name - %s\r", argv[optind]);
		err(buffer);
	}

	if (rose_aton(addr, rosebind.srose_addr.rose_addr) == -1) {
		sprintf(buffer, "ERROR: invalid Rose port address - %s\r", argv[optind]);
		err(buffer);
	}

	if (ax25_aton_entry(argv[optind + 1], rosebind.srose_call.ax25_call) == -1) {
		sprintf(buffer, "ERROR: invalid callsign - %s\r", argv[optind + 1]);
		err(buffer);
	}

	if (ax25_aton_entry(argv[optind + 2], roseconnect.srose_call.ax25_call) == -1) {
		sprintf(buffer, "ERROR: invalid callsign - %s\r", argv[optind + 2]);
		err(buffer);
	}

	if (rose_aton(argv[optind + 3], roseconnect.srose_addr.rose_addr) == -1) {
		sprintf(buffer, "ERROR: invalid Rose address - %s\r", argv[optind + 3]);
		err(buffer);
	}

	/*
	 * Open the socket into the kernel.
	 */
	if ((s = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0) {
		sprintf(buffer, "ERROR: cannot open Rose socket, %s\r", strerror(errno));
		err(buffer);
	}

	/*
	 * Set our AX.25 callsign and Rose address accordingly.
	 */
	if (bind(s, (struct sockaddr *)&rosebind, addrlen) != 0) {
		sprintf(buffer, "ERROR: cannot bind Rose socket, %s\r", strerror(errno));
		err(buffer);
	}

	sprintf(buffer, "Connecting to %s @ %s ...\r", argv[optind + 2], argv[optind + 3]);
	user_write(STDOUT_FILENO, buffer, strlen(buffer));

	/*
	 * If no response in 30 seconds, go away.
	 */
	alarm(30);

	signal(SIGALRM, alarm_handler);

	/*
	 * Lets try and connect to the far end.
	 */
	if (connect(s, (struct sockaddr *)&roseconnect, addrlen) != 0) {
		switch (errno) {
			case ECONNREFUSED:
				strcpy(buffer, "*** Connection refused - aborting\r");
				break;
			case ENETUNREACH:
				strcpy(buffer, "*** No known route - aborting\r");
				break;
			case EINTR:
				strcpy(buffer, "*** Connection timed out - aborting\r");
				break;
			default:
				sprintf(buffer, "ERROR: cannot connect to Rose address, %s\r", strerror(errno));
				break;
		}

		err(buffer);
	}

	/*
	 * We got there.
	 */
	alarm(0);

	strcpy(buffer, "*** Connected\r");
	user_write(STDOUT_FILENO, buffer, strlen(buffer));

	select_loop(s);

	strcpy(buffer, "\r*** Disconnected\r");
	user_write(STDOUT_FILENO, buffer, strlen(buffer));

	end_compress();

	return 0;
}
