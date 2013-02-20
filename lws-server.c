/*
 * libwebsockets-test-server - libwebsockets test implementation
 *
 * Copyright (C) 2010-2011 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */
#ifdef CMAKE_BUILD
#include "lws_config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#ifdef WIN32

#ifdef EXTERNAL_POLL
	#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <stddef.h>

	#include "websock-w32.h"
#endif

#else // NOT WIN32
#include <syslog.h>
#endif

#include <signal.h>

#include "../lib/libwebsockets.h"
#include "HierarchicalHash.h"

static int close_testing;
int max_poll_elements;

struct pollfd *pollfds;
int *fd_lookup;
int count_pollfds;
int force_exit = 0;

int number_connections = 0;
int max_connections = 5;

HierarchicalHash *lastmsg;

/*
 * This demo server shows how to use libwebsockets for one or more
 * websocket protocols in the same server
 *
 * It defines the following websocket protocols:
 *
 *  dumb-increment-protocol:  once the socket is opened, an incrementing
 *				ascii string is sent down it every 50ms.
 *				If you send "reset\n" on the websocket, then
 *				the incrementing number is reset to 0.
 *
 *  lws-mirror-protocol: copies any received packet to every connection also
 *				using this protocol, including the sender
 */

enum demo_protocols {
	/* always first */
	PROTOCOL_HTTP = 0,

	PROTOCOL_LWS_MIRROR,

	/* always last */
	DEMO_PROTOCOL_COUNT
};


#define LOCAL_RESOURCE_PATH "."


struct per_session_data__http {
	int fd;
};

/* this protocol server (always the first one) just knows how to do HTTP */

static int callback_http(struct libwebsocket_context *context,
		struct libwebsocket *wsi,
		enum libwebsocket_callback_reasons reason, void *user,
							   void *in, size_t len)
{
#if 0
	char client_name[128];
	char client_ip[128];
#endif
	char buf[256];
	int n;
	unsigned char *p;
	static unsigned char buffer[8192];
	struct stat stat_buf;
	struct per_session_data__http *pss = (struct per_session_data__http *)user;

        char resource_name[512];
        char *extension;
        char mime[256];

	switch (reason) {
	case LWS_CALLBACK_HTTP:

                strcpy(resource_name, LOCAL_RESOURCE_PATH);
		strcat(resource_name, (char *) in);

                extension = strrchr(resource_name, '.');
                // choose mime type based on the file extension
                if (extension == NULL) {
                        strncpy(mime,"text/plain",255);
                } else if (strcmp(extension, ".png") == 0) {
                        strncpy(mime,"image/png",255);
                } else if (strcmp(extension, ".jpg") == 0) {
                        strncpy(mime,"image/jpg",255);
                } else if (strcmp(extension, ".gif") == 0) {
                        strncpy(mime,"image/gif",255);
                } else if (strcmp(extension, ".html") == 0) {
                        strncpy(mime,"text/html",255);
                } else if (strcmp(extension, ".css") == 0) {
                        strncpy(mime,"text/css",255);
                } else if (strcmp(extension, ".js") == 0) {
                        strncpy(mime,"application/x-javascript",255);
                } else {
                        strncpy(mime,"text/plain",255);
                }


                fprintf(stderr, "serving HTTP URI %s\n", resource_name);
		if (libwebsockets_serve_http_file(context, wsi, resource_name, mime))
			return -1; /* through completion or error, close the socket */

		/*
		 * notice that the sending of the file completes asynchronously,
		 * we'll get a LWS_CALLBACK_HTTP_FILE_COMPLETION callback when
		 * it's done
		 */

		break;

	case LWS_CALLBACK_HTTP_FILE_COMPLETION:
//		lwsl_info("LWS_CALLBACK_HTTP_FILE_COMPLETION seen\n");
		/* kill the connection after we sent one file */
		return -1;

	default:
		break;
	}

	return 0;
}



/* lws-mirror_protocol */

#define MAX_MESSAGE_QUEUE 128

struct per_session_data__lws_mirror {
	struct libwebsocket *wsi;
	int ringbuffer_tail;
	char name[127];
};

struct a_message {
	void *payload;
	size_t len;
};

static struct a_message ringbuffer[MAX_MESSAGE_QUEUE];
static int ringbuffer_head;

static struct libwebsocket *wsi_choked[20];
static int num_wsi_choked;

void
send_msg(struct libwebsocket *wsi, 
		per_session_data__lws_mirror *pss, 
		const char *in, 
		int len)
{
	if (((ringbuffer_head - pss->ringbuffer_tail) &
			  (MAX_MESSAGE_QUEUE - 1)) == (MAX_MESSAGE_QUEUE - 1)) {
		lwsl_err("dropping!\n");
		goto choke;
	}

	if (ringbuffer[ringbuffer_head].payload)
		free(ringbuffer[ringbuffer_head].payload);

	ringbuffer[ringbuffer_head].payload =
			malloc(LWS_SEND_BUFFER_PRE_PADDING + len +
					  LWS_SEND_BUFFER_POST_PADDING);
	ringbuffer[ringbuffer_head].len = len;
	memcpy((char *)ringbuffer[ringbuffer_head].payload +
				  LWS_SEND_BUFFER_PRE_PADDING, in, len);
	if (ringbuffer_head == (MAX_MESSAGE_QUEUE - 1))
		ringbuffer_head = 0;
	else
		ringbuffer_head++;

	if (((ringbuffer_head - pss->ringbuffer_tail) &
			  (MAX_MESSAGE_QUEUE - 1)) != (MAX_MESSAGE_QUEUE - 2))
		goto done;

choke:
	if (num_wsi_choked < sizeof wsi_choked / sizeof wsi_choked[0]) {
		libwebsocket_rx_flow_control(wsi, 0);
		wsi_choked[num_wsi_choked++] = wsi;
	}

//		lwsl_debug("rx fifo %d\n", (ringbuffer_head - pss->ringbuffer_tail) & (MAX_MESSAGE_QUEUE - 1));
done:
	libwebsocket_callback_on_writable_all_protocol(
				       libwebsockets_get_protocol(wsi));
}

static int
callback_lws_mirror(struct libwebsocket_context *context,
			struct libwebsocket *wsi,
			enum libwebsocket_callback_reasons reason,
					       void *user, void *in, size_t len)
{
	HierarchicalHash *msg;

	int n;
	struct per_session_data__lws_mirror *pss = (struct per_session_data__lws_mirror *)user;

	switch (reason) {

	case LWS_CALLBACK_ESTABLISHED:
		lwsl_info("callback_lws_mirror: "
						 "LWS_CALLBACK_ESTABLISHED\n");
		pss->ringbuffer_tail = ringbuffer_head;
		pss->wsi = wsi;
		break;

	case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
		if (number_connections >= max_connections) {
			fprintf(stderr, "Connection refused, %d connections.\n", number_connections);
			return -1;
		}
		number_connections++;
		fprintf(stderr, "Connection established, %d connections.\n", number_connections);
		break;


	case LWS_CALLBACK_PROTOCOL_DESTROY:
		lwsl_notice("mirror protocol cleaning up\n");
		for (n = 0; n < sizeof ringbuffer / sizeof ringbuffer[0]; n++)
			if (ringbuffer[n].payload)
				free(ringbuffer[n].payload);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		if (close_testing)
			break;
		while (pss->ringbuffer_tail != ringbuffer_head) {

			n = libwebsocket_write(wsi, (unsigned char *)
				   ringbuffer[pss->ringbuffer_tail].payload +
				   LWS_SEND_BUFFER_PRE_PADDING,
				   ringbuffer[pss->ringbuffer_tail].len,
								LWS_WRITE_TEXT);
			if (n < 0) {
				lwsl_err("ERROR %d writing to socket\n", n);
				return 1;
			}

			if (pss->ringbuffer_tail == (MAX_MESSAGE_QUEUE - 1))
				pss->ringbuffer_tail = 0;
			else
				pss->ringbuffer_tail++;

			if (((ringbuffer_head - pss->ringbuffer_tail) &
				  (MAX_MESSAGE_QUEUE - 1)) == (MAX_MESSAGE_QUEUE - 15)) {
				for (n = 0; n < num_wsi_choked; n++)
					libwebsocket_rx_flow_control(wsi_choked[n], 1);
				num_wsi_choked = 0;
			}
			// lwsl_debug("tx fifo %d\n", (ringbuffer_head - pss->ringbuffer_tail) & (MAX_MESSAGE_QUEUE - 1));

			if (lws_send_pipe_choked(wsi)) {
				libwebsocket_callback_on_writable(context, wsi);
				return 0;
			}
		}
		break;

	case LWS_CALLBACK_RECEIVE:
	        fprintf(stderr, "%s\n", (char *) in);


		msg = new HierarchicalHash((const char *) in);
		if (msg->Exists("name"))
			strncpy(pss->name,msg->Get("name"),126);
			lastmsg->Set(msg->Get("name"),(char *) in);
		if (msg->Exists("delete"))
			lastmsg->Delete(msg->Get("delete"));

		if (msg->Exists("refresh")) {
			HierarchicalHashIterator *i = new HierarchicalHashIterator(lastmsg);
			 while (!i->End()) {
			         char *msg = i->NextValue();
			         send_msg(wsi, pss, msg, strlen(msg));
			         //printf("  %s\n",msg);
			}
			msg->~HierarchicalHash();
			return 0;
		}

		msg->~HierarchicalHash();
		
		send_msg(wsi, pss, (const char *) in, len);

		break;
		
	case LWS_CALLBACK_CLOSED:
		char deletemsg[50];
		sprintf(deletemsg, "{\"delete\":\"%s\"}",pss->name); 
		send_msg(wsi, pss, deletemsg, strlen(deletemsg));
		number_connections--;
		fprintf(stderr, "Connection closed, %d connections remain.\n", number_connections);
		return -1;
		break;

	default:
		break;
	}

	return 0;
}


/* list of supported protocols and callbacks */

static struct libwebsocket_protocols protocols[] = {
	/* first protocol must always be HTTP handler */

	{
		"http-only",		/* name */
		callback_http,		/* callback */
		sizeof (struct per_session_data__http),	/* per_session_data_size */
		0,			/* max frame size / rx buffer */
	},
	{
		"lws-mirror-protocol",
		callback_lws_mirror,
		sizeof(struct per_session_data__lws_mirror),
		128,
	},
	{ NULL, NULL, 0, 0 } /* terminator */
};

void sighandler(int sig)
{
	force_exit = 1;
}

static struct option options[] = {
	{ "help",	no_argument,		NULL, 'h' },
	{ "debug",	required_argument,	NULL, 'd' },
	{ "port",	required_argument,	NULL, 'p' },
	{ "ssl",	no_argument,		NULL, 's' },
	{ "interface",  required_argument,	NULL, 'i' },
	{ "closetest",  no_argument,		NULL, 'c' },
#ifndef LWS_NO_DAEMONIZE
	{ "daemonize", 	no_argument,		NULL, 'D' },
#endif
	{ NULL, 0, 0, 0 }
};

int main(int argc, char **argv)
{
	lastmsg = new HierarchicalHash();
	int n = 0;
	int use_ssl = 0;
	struct libwebsocket_context *context;
	int opts = 0;
	char interface_name[128] = "";
	const char *iface = NULL;
#ifndef WIN32
	int syslog_options = LOG_PID | LOG_PERROR;
#endif
	unsigned int oldus = 0;
	struct lws_context_creation_info info;

	int debug_level = 0;
#ifndef LWS_NO_DAEMONIZE
	int daemonize = 0;
#endif

	memset(&info, 0, sizeof info);
	info.port = 7681;
	info.ka_time = 3;
	info.ka_probes = 3;
	info.ka_interval = 1;

	while (n >= 0) {
		n = getopt_long(argc, argv, "ci:hsp:d:D", options, NULL);
		if (n < 0)
			continue;
		switch (n) {
#ifndef LWS_NO_DAEMONIZE
		case 'D':
			daemonize = 1;
			#ifndef WIN32
			syslog_options &= ~LOG_PERROR;
			#endif
			break;
#endif
		case 'd':
			debug_level = atoi(optarg);
			break;
		case 's':
			use_ssl = 1;
			break;
		case 'p':
			info.port = atoi(optarg);
			break;
		case 'i':
			strncpy(interface_name, optarg, sizeof interface_name);
			interface_name[(sizeof interface_name) - 1] = '\0';
			iface = interface_name;
			break;
		case 'c':
			close_testing = 1;
			fprintf(stderr, " Close testing mode -- closes on "
					   "client after 50 dumb increments"
					   "and suppresses lws_mirror spam\n");
			break;
		case 'h':
			fprintf(stderr, "Usage: test-server "
					"[--port=<p>] [--ssl] "
					"[-d <log bitfield>]\n");
			exit(1);
		}
	}

#if !defined(LWS_NO_DAEMONIZE) && !defined(WIN32)
	/* 
	 * normally lock path would be /var/lock/lwsts or similar, to
	 * simplify getting started without having to take care about
	 * permissions or running as root, set to /tmp/.lwsts-lock
	 */
	if (daemonize && lws_daemonize("/tmp/.lwsts-lock")) {
		fprintf(stderr, "Failed to daemonize\n");
		return 1;
	}
#endif

	signal(SIGINT, sighandler);

#ifndef WIN32
	/* we will only try to log things according to our debug_level */
	setlogmask(LOG_UPTO (LOG_DEBUG));
	openlog("lwsts", syslog_options, LOG_DAEMON);
#endif

	/* tell the library what debug level to emit and to send it to syslog */
	lws_set_log_level(debug_level, lwsl_emit_syslog);
	printf("lws server: listening on port %d, serving files from %s/ ...\n",info.port,LOCAL_RESOURCE_PATH);
	lwsl_notice("libwebsockets test server - "
			"(C) Copyright 2010-2013 Andy Green <andy@warmcat.com> - "
						    "licensed under LGPL2.1\n");

	info.iface = iface;
	info.protocols = protocols;
#ifndef LWS_NO_EXTENSIONS
	info.extensions = libwebsocket_get_internal_extensions();
#endif
	if (!use_ssl) {
		info.ssl_cert_filepath = NULL;
		info.ssl_private_key_filepath = NULL;
	} else {
		info.ssl_cert_filepath = LOCAL_RESOURCE_PATH"/libwebsockets-test-server.pem";
		info.ssl_private_key_filepath = LOCAL_RESOURCE_PATH"/libwebsockets-test-server.key.pem";
	}
	info.gid = -1;
	info.uid = -1;
	info.options = opts;

	context = libwebsocket_create_context(&info);
	if (context == NULL) {
		lwsl_err("libwebsocket init failed\n");
		return -1;
	}

	n = 0;
	while (n >= 0 && !force_exit) {
		struct timeval tv;

		gettimeofday(&tv, NULL);

		/*
		 * If libwebsockets sockets are all we care about,
		 * you can use this api which takes care of the poll()
		 * and looping through finding who needed service.
		 *
		 * If no socket needs service, it'll return anyway after
		 * the number of ms in the second argument.
		 */

		n = libwebsocket_service(context, 50);
	}


	libwebsocket_context_destroy(context);

	lwsl_notice("libwebsockets-test-server exited cleanly\n");

#ifndef WIN32
	closelog();
#endif

	return 0;
}
