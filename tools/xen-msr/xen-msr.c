#define _GNU_SOURCE

#include <alloca.h>
#include <getopt.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "xen-msr.h"


void fatal(const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "xen-msr: ");

	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);

	fprintf(stderr, "\nPlease type 'xen-msr --help' for more "
		"informations\n");

	exit(EXIT_FAILURE);
}

void usage(void)
{
	printf("Usage: xen-msr [ <options> ] <commands>\nRead and write the "
	       "machine specific register throught Xen hypercalls. These\n"
	       "registers can be used via a list of commands. Each command "
	       "specify what to do\nwith what register at what time.\n"
	       "Only raw registers are supported and the user should refer "
	       "to the reference\nmanual of its machine to know what register "
	       "to use.\n\n");
	printf("Command format:  [ : ] <address> [ = <value> ] [ @ <time> "
	       "[ - <interval> ] ]\n\n"
	       "  - A command always contain a register address. This address "
	       "can be specified\n    in decimal or hexadecimal notation.\n\n"
	       "    390     -   access to the register 390 but do nothing "
	       "with it\n    x186    -   access to the register 0x186 but do "
	       "nothing with it\n\n");
	printf("  - The register address can be prefixed by a \":\" symbol. "
	       "This indicate the\n    value of the register has to be "
	       "printed.\n\n    :x186   -   read the register 0x186 and print "
	       "the value\n\n");
	printf("  - The register address can be postfixed by a \"=\" symbol "
	       "followed by a value\n    in decimal or hexadecimal notation. "
	       "This indicate the value to write in the\n    register. If the "
	       "register is prefixed by \":\", the old value is printed,\n");
	printf("    then the new value is written in the regsiter.\n\n"
	       "    x186=10   -   write the value 10 in the register 0x186\n"
	       "    x0c1=xa   -   write the value 0xa in the register 0x186\n"
	       "    :x186=0   -   read the value in the register 0x186 and "
	       "print it, then write\n                  0 in the "
	       "register\n\n");
	printf("  - A command can be postfixed with a time indication, which "
	       "specify the time\n    the command has to be executed at. "
	       "This indication is a \"@\" symbol\n    followed by a count "
	       "of millisecond in decimal or hexadecimal notation.\n\n"
	       "    x186=42       -   write the value 42 in the register "
	       "0x186 at the start\n                      of the program\n"
	       "    x186=42@300   -   wait 300 millisecond, the write the "
	       "value 42 in the\n                      register 0x186\n\n");
	printf("  - A time indication can be postfixed by a \"-\" symbol and "
	       "an interval value.\n    The interval value is a count of "
	       "millisecond in decimal or hexadecimal\n    notation "
	       "indicating a period of time the command has to be executed "
	       "at.\n\n");
	printf("    :x0c1@0-100         -   read the value in the register "
	       "0x0c1 and print it\n                            every 100 "
	       "milliseconds\n    :x186=12@1500-200   -   wait 1500 "
	       "milliseconds, then every 200\n"
	       "                            milliseconds, read the value in "
	       "the register 0x186,\n                            print it, "
	       "then write 12 in the register\n\n");
	printf("Options:\n\n  -h, --help      Print this help message, then "
	       "exit immediately.\n\n  -c, --core      Specify on what core "
	       "(physical core) to control the\n                  registers. "
	       "It can be a number, starting from 0, or the string\n"
	       "                  \"all\".\n");
}

unsigned long xennrcpus(void)
{
	unsigned long ncpus = 0;
	int pipes[2];
	char buffer[4096];
	int state;
	char *str;

	if (pipe(pipes) != 0)
		return 0;

	if (fork() == 0) {
		close(STDOUT_FILENO);
		dup2(pipes[1], STDOUT_FILENO);
		execlp("xl", "xl", "info", NULL);
	} else {
		wait(&state);
		if (WEXITSTATUS(state))
			return 0;
		read(pipes[0], buffer, sizeof(buffer));
		buffer[4095] = 0;

		str = strstr(buffer, "nr_cpus");
		if (str == NULL)
			return 0;

		str += strlen("nr_cpus");
		while (*str++ != ':')
			;
		while (*str < '0' && *str > '9')
			str++;

		ncpus = strtol(str, NULL, 10);
	}

	return ncpus;
}

int main(int argc, char *const *argv)
{
	struct option options[] = {
		{"help",   no_argument,       0, 'h'},
		{"core",   required_argument, 0, 'c'},
		{ NULL,    0,                 0,  0 }
	};
	struct command *cmds = alloca(argc * sizeof(struct command));
	unsigned long nrcpus = 0, corec;
	unsigned long *core_ids;
	unsigned long cmdc = 0;
	char *core_used = NULL;
	unsigned long j;
	int i, c, ret;
	char *err;


	do {
		c = getopt_long(argc, argv, "hc:", options, NULL);

		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'c':
			if (nrcpus == 0) {
				nrcpus = xennrcpus();
				if (nrcpus == 0)
					fatal("unabled to read the cpu count");
				core_used = alloca(nrcpus);
				memset(core_used, 0, nrcpus);
			}

			if (!strcmp(optarg, "all")) {
				memset(core_used, 1, nrcpus);
			} else {
				i = strtol(optarg, &err, 10);
				if (*err)
					fatal("invalid core option: '%s'",
					      optarg);
				core_used[i] = 1;
			}
			break;
		case '?':
			return EXIT_FAILURE;
		}
	} while (c != -1);

	if (optind >= argc)
		fatal("no specified command");

	for (i=optind; i<argc; i++) {
		ret = parse_command(&cmds[cmdc++], argv[i]);
		if (ret != 0)
			fatal("invalid command: '%s'", argv[i]);
	}

	corec = 0;
	for (j=0; j<nrcpus; j++)
		if (core_used[j])
			corec++;
	if (corec == 0) {
		if (nrcpus == 0) {
			nrcpus = xennrcpus();
			if (nrcpus == 0)
				fatal("unabled to read the cpu count");
			core_used = alloca(nrcpus);
			memset(core_used, 0, nrcpus);
		}
		core_used[sched_getcpu()] = 1;
		corec++;
	}

	core_ids = alloca(corec * sizeof(unsigned long));
	corec = 0;
	for (j=0; j<nrcpus; j++)
		if (core_used[j])
			core_ids[corec++] = j;

	execute(cmds, cmdc, core_ids, corec);

	return EXIT_SUCCESS;
}
