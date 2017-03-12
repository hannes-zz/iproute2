#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>

#include "utils.h"
#include "ip_common.h"
#include "namespace.h"
#include "afnetns.h"

static void usage(void)
{
	static const char *help =
		"Usage: ip afnetns list\n"
		"       ip afnetns add NAME\n"
		"       ip afnetns del NAME\n"
		"       ip afnetns exec NAME cmd ...\n";
	fputs(help, stderr);
}

static int afnetns_list(void)
{
	struct dirent *entry;
	DIR *dir;

	dir = opendir(AFNETNS_RUN_DIR);
	if (!dir)
		return 0;

	while ((entry = readdir(dir))) {
		if (!strcmp(entry->d_name, ".") ||
		    !strcmp(entry->d_name, ".."))
			continue;
		printf("%s\n", entry->d_name);
	}
	closedir(dir);

	return 0;
}

static int create_afnetns_dir(void)
{
	int err;
	const mode_t mode = S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;

	err = mkdir(AFNETNS_RUN_DIR, mode);
	if (!err || errno == EEXIST)
		return 0;

	fprintf(stderr, "Could not create afnet run dir \"%s\": %s\n",
		AFNETNS_RUN_DIR, strerror(errno));
	return err;
}

static int afnetns_delete(int argc, char **argv)
{
	const char *name;
	char *path;
	int err;

	if (argc < 1) {
		fputs("No afnetns name specified\n", stderr);
		return -1;
	}

	name = argv[0];
	err = asprintf(&path, "%s/%s", AFNETNS_RUN_DIR, name);
	if (err < 0) {
		perror("asprintf");
		return err;
	}

	err = umount2(path, MNT_DETACH);
	if (err)
		fprintf(stderr, "Cannot umount afnet namespace file \"%s\": %s\n",
			path, strerror(errno));

	err = unlink(path);
	if (err) {
		fprintf(stderr, "Cannot remove afnet namespace file \"%s\": %s\n",
			path, strerror(errno));
		goto out;
	}

out:
	free(path);
	return err;
}

static int afnetns_add(int argc, char **argv)
{
	const char *name;
	int err, fd;
	char *path;

	if (argc < 1) {
		fputs("No afnetns name specified\n", stderr);
		return -1;
	}

	err = create_afnetns_dir();
	if (err)
		return err;

	name = argv[0];
	err = asprintf(&path, "%s/%s", AFNETNS_RUN_DIR, name);
	if (err < 0) {
		perror("asprintf");
		return err;
	}

	fd = open(path, O_RDONLY|O_CREAT|O_EXCL, 0);
	if (fd < 0) {
		err = fd;
		fprintf(stderr, "Cannot create afnetns file \"%s\": %s\n",
			path, strerror(errno));
		goto out;
	}
	err = close(fd);
	if (err) {
		perror("close");
		goto out;
	}

	err = unshare(CLONE_NEWAFNET);
	if (err < 0) {
		fprintf(stderr, "Failed to create a new afnet namesapce \"%s\": %s\n",
			name, strerror(errno));
		goto out;
	}

	err = mount("/proc/self/ns/afnet", path, "none", MS_BIND, NULL);
	if (err < 0) {
		fprintf(stderr, "Bind /proc/self/ns/afnet -> %s failed: %s\n",
			path, strerror(errno));
		goto out_delete;
	}

	err = 0;
out:
	free(path);
	return err;
out_delete:
	afnetns_delete(argc, argv);
	goto out;
}

static int afnetns_switch(const char *name)
{
	int err, ns;

	ns = afnetns_open(name);
	if (ns < 0)
		return ns;

	err = setns(ns, CLONE_NEWAFNET);
	if (err) {
		fprintf(stderr, "setting the afnet namespace \"%s\" failed: %s\n",
			name, strerror(errno));
		return err;
	}

	err = close(ns);
	if (err) {
		perror("close");
		return err;
	}

	return 0;
}

static int afnetns_exec(int argc, char **argv)
{
	const char *cmd;
	int err;

	if (argc < 2) {
		fputs("No netns name and or commands specified\n", stderr);
		return -1;
	}

	err = afnetns_switch(argv[0]);
	if (err)
		return err;

	cmd = argv[1];
	return -cmd_exec(cmd, argv + 1, !!batch_mode);
}

int do_afnetns(int argc, char **argv)
{
	if (argc < 1)
		return afnetns_list();

	if (!matches(*argv, "help")) {
		usage();
		return 0;
	}

	if (!matches(*argv, "list") || !matches(*argv, "show") ||
	    !matches(*argv, "lst"))
		return afnetns_list();

	if (!matches(*argv, "add"))
		return afnetns_add(argc-1, argv+1);

	if (!matches(*argv, "delete"))
		return afnetns_delete(argc-1, argv+1);

	if (!matches(*argv, "exec"))
		return afnetns_exec(argc-1, argv+1);

	fprintf(stderr, "Command \"%s\" is unkown, try \"ip afnetns help\".\n", *argv);
	return -1;
}
