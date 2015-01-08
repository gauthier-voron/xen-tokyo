#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xenctrl.h>
#include <xenguest.h>
#include <xc_private.h>


#define HYPERCALL_CMD_START_MONITORING   ((unsigned long)  -7)
#define HYPERCALL_CMD_STOP_MONITORING    ((unsigned long)  -8)
#define HYPERCALL_CMD_DECIDE_MIGR        ((unsigned long)  -9)
#define HYPERCALL_CMD_PERFORM_MIGR       ((unsigned long) -10)


static void usage(FILE *stream)
{
	fprintf(stream, "Usage: xen-automem [ option... ] decide [ perform ]\n"
		"Communicate with Xen to perform memory migration accordingly "
		"to the need of the\nrunning HVM guests. Communicate with Xen "
		"throught hypercalls every specified\ninterval for decisions "
		"and actual migrations (in milliseconds)\n\n");
	fprintf(stream, "Options:\n  -h, -?, --help                   "
		"display this help message, then exit\n"
		"                                   immediately\n\n"
		"  -v, --version                    display the "
		"version number of this program,\n"
		"                                   then exit "
		"immediately\n\n  -t, --tracked size               "
		"specify the amount of page per cpu which can\n"
		"                                   be tracked by the "
		"hotlist\n\n  -c, --candidates size            "
		"specify the amount of page in the hotlists\n"
		"                                   which can be "
		"candidate for migration, where\n"
		"                                   hottest pages "
		"have more priority\n\n");
	fprintf(stream,
		"  -q, --enqueued size              specify the "
		"amount of page which can be\n"
		"                                   migrated per "
		"hypercall\n\n  -l, --hotlist ent:incr:decr:max  "
		"specify the hotlist parameters, separated by\n"
		"                                   columns: the "
		"enter score, increment score,\n"
		"                                   decrement score "
		"and maximum score of pages\n"
		"                                   in the hotlist\n\n"
		"  -m, --migration lsc:lrt:flsh     specify the "
		"migration engine paramters,\n"
		"                                   separated by "
		"columns: the local score and\n"
		"                                   local rate needed "
		"to be migrated, and if\n"
		"                                   the hotlists are "
		"flushed after a migration\n\n"
		"  -r --maxtries                    specify the amount of "
		"hypercall a migration\n"
		"                                   query can stay queued "
		"before to be aborted\n\n"
		"  -s --sampling                    specify the sampling rate "
		"where a low rate\n"
		"                                   indicates frequent "
		"samples\n");
}

static void version(FILE *stream)
{
	fprintf(stream, "xen-automem 1.0.0 Gauthier Voron\n"
		"gauthier.voron@lip6.fr\n");
}

static void error(const char *reason, ...)
{
	va_list ap;

	va_start(ap, reason);
	fprintf(stderr, "xen-automem: ");
	vfprintf(stderr, reason, ap);
	fprintf(stderr, "\nplease type 'xen-automem --help' for more "
		"informations\n");
	va_end(ap);

	exit(EXIT_FAILURE);
}


static int hypercall(unsigned long command, unsigned long *args, int *ret)
{
    xc_interface *xch = xc_interface_open(0, 0, 0);

    DECLARE_HYPERCALL;

    if (xch == NULL)
    	    return -1;

    hypercall.op = __HYPERVISOR_xen_version;
    hypercall.arg[0] = command;
    hypercall.arg[1] = (unsigned long) args;

    *ret = do_xen_hypercall(xch, &hypercall);

    xc_interface_close(xch);

    return 0;
}


static int parse_numbers(unsigned long *nums, size_t len, const char *str)
{
	size_t i = 0;
	char *nxt;
	
	while (len > 0) {
		nums[i++] = strtol(str, &nxt, 10);
		len--;

		if (len > 0 && *nxt != ':')
			return -1;
		else if (len == 0 && *nxt != '\0')
			return -1;
		
		str = (const char *) nxt + 1;
	}

	return 0;
}


static volatile int continue_migration = 1;

static void sighandler(int signum __attribute__((unused)))
{
	continue_migration = 0;
}

static void perform_hypercalls(unsigned long *params, unsigned long decide,
			       unsigned long perform)
{
	int ret;
	unsigned long now, goal, goal_decide, goal_perform;
	struct timespec ts;
	struct sigaction sigact;

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = sighandler;

	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGSTOP, &sigact, NULL);
	
	if (hypercall(HYPERCALL_CMD_START_MONITORING, params, &ret) != 0)
		error("failed to communicate with Xen");
	if (ret != 0)
		error("failed to start monitoring");

	clock_gettime(CLOCK_REALTIME, &ts);
	now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000ul;
	goal_decide = now;
	goal_perform = now;

	while (continue_migration) {
		clock_gettime(CLOCK_REALTIME, &ts);
		now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000ul;

		if (goal_perform <= now) {
			if (hypercall(HYPERCALL_CMD_PERFORM_MIGR, NULL, &ret))
				error("failed to communicate with Xen");
			if (ret != 0)
				break;
		}

		if (goal_decide <= now) {
			if (hypercall(HYPERCALL_CMD_DECIDE_MIGR, NULL, &ret))
				error("failed to communicate with Xen");
			if (ret != 0)
				break;
		}


		clock_gettime(CLOCK_REALTIME, &ts);
		now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000ul;

		while (goal_decide <= now)
			goal_decide += decide;
		while (goal_perform <= now)
			goal_perform += perform;

		goal = goal_perform;
		if (goal_decide < goal)
			goal = goal_decide;

		ts.tv_sec = ((goal - now) / 1000);
		ts.tv_nsec = ((goal - now) % 1000) * 1000000ul;
		nanosleep(&ts, NULL);
	}
	
	if (hypercall(HYPERCALL_CMD_STOP_MONITORING, NULL, &ret) != 0)
		error("failed to communicate with Xen");
	if (ret != 0)
		error("failed to start monitoring");
}

int main(int argc, char * const* argv)
{
	int c;
	unsigned char tracked_opt = 0, candidates_opt = 0, enqueued_opt = 0;
	unsigned char hotlist_opt = 0, migration_opt = 0, maxtries_opt = 0;
	unsigned char rate_opt = 0;
	unsigned long tracked = 512, candidates = 32, enqueued = 4;
	unsigned long hotlist[4] = {8, 8, 1, 1024};
	unsigned long migration[3] = {256, 90, 0};
	unsigned long maxtries = 4;
	unsigned long rate = 0x80000;
	unsigned long decide, perform;
	unsigned long hypercall_params[12];
	
	struct option options[] = {
		{"help",       no_argument,       0, 'h'},
		{"version",    no_argument,       0, 'v'},
		{"tracked",    required_argument, 0, 't'},
		{"candidates", required_argument, 0, 'c'},
		{"enqueued",   required_argument, 0, 'q'},
		{"hotlist",    required_argument, 0, 'l'},
		{"migration",  required_argument, 0, 'm'},
		{"maxtries",   required_argument, 0, 'r'},
		{"sampling",   required_argument, 0, 's'},
		{ NULL,        0,                 0,  0 }
	};

	while (1) {
		c = getopt_long(argc,argv, "h?vt:c:q:l:m:r:s:", options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
		case '?':
			usage(stdout);
			return EXIT_SUCCESS;
		case 'v':
			version(stdout);
			return EXIT_SUCCESS;
		case 't':
			if (tracked_opt++ > 0)
				error("option 'tracked' specified twice");
			if (parse_numbers(&tracked, 1, optarg) != 0)
				error("invalid 'tracked' parameter: '%s'",
				      optarg);
			break;
		case 'c':
			if (candidates_opt++ > 0)
				error("option 'candidates' specified twice");
			if (parse_numbers(&candidates, 1, optarg) != 0)
				error("invalid 'candidates' parameter: '%s'",
				      optarg);
			break;
		case 'q':
			if (enqueued_opt++ > 0)
				error("option 'enqueued' specified twice");
			if (parse_numbers(&enqueued, 1, optarg) != 0)
				error("invalid 'enqueued' parameter: '%s'",
				      optarg);
			break;
		case 'l':
			if (hotlist_opt++ > 0)
				error("option 'hotlist' specified twice");
			if (parse_numbers(hotlist, 4, optarg) != 0)
				error("invalid 'hotlist' parameter: '%s'",
				      optarg);
			break;
		case 'm':
			if (migration_opt++ > 0)
				error("option 'migration' specified twice");
			if (parse_numbers(migration, 3, optarg) != 0)
				error("invalid 'migration' parameter: '%s'",
				      optarg);
			break;
		case 'r':
			if (maxtries_opt++ > 0)
				error("option 'maxtries' specified twice");
			if (parse_numbers(&maxtries, 1, optarg) != 0)
				error("invalid 'maxtries' parameter: '%s'",
				      optarg);
			break;
		case 's':
			if (rate_opt++ > 0)
				error("option 'sampling' specified twice");
			if (parse_numbers(&rate, 1, optarg) != 0)
				error("invalid 'sampling' parameter: '%s'",
				      optarg);
			break;
		}
	}

	if (optind >= argc)
		error("missing operand 'decide'");
	if (parse_numbers(&decide, 1, argv[optind]) != 0 || decide == 0)
		error("invalid operand 'decide': '%s'", argv[optind]);

	if (optind + 1 >= argc)
		perform = decide;
	else if (parse_numbers(&perform, 1, argv[optind+1]) != 0
		 || perform == 0)
		error("invalid operand 'perform': '%s'", argv[optind+1]);

	hypercall_params[0]  = tracked;
	hypercall_params[1]  = candidates;
	hypercall_params[2]  = enqueued;
	hypercall_params[3]  = hotlist[0];
	hypercall_params[4]  = hotlist[1];
	hypercall_params[5]  = hotlist[2];
	hypercall_params[6]  = hotlist[3];
	hypercall_params[7]  = migration[0];
	hypercall_params[8]  = migration[1];
	hypercall_params[9]  = migration[2];
	hypercall_params[10] = maxtries;
	hypercall_params[11] = rate;

	perform_hypercalls(hypercall_params, decide, perform);

	return EXIT_SUCCESS;
}
