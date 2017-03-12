#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

#include <math.h>

#include "list.h"
#include "afnetns.h"

#define ULONG_CHARS ((int)ceill(log10l(ULONG_MAX)))

static struct inode_cache {
	struct inode_cache *next;
	ino_t inode;
	char name[];
} *cache[64];

static int self_inode(ino_t *me)
{
	static bool initialized;
	static ino_t inode;
	long path_size;
	char *path;
	int err;

	if (initialized) {
		*me = inode;
		return 0;
	}

	errno = 0;
	path_size = pathconf("/proc/self/ns/afnet", _PC_PATH_MAX);
	if (path_size < 0) {
		if (errno)
			perror("pathconf");
		else
			fprintf(stderr,
				"couldn't determine _PC_PATH_MAX for procfs: %zd\n",
				path_size);
		return -1;
	}

	path = malloc(path_size);
	if (!path) {
		perror("malloc");
		return -1;
	}

	err = readlink("/proc/self/ns/afnet", path, path_size);
	if (err < 0) {
		perror("readlink");
		goto out;
	} else if (err >= path_size) {
		fprintf(stderr, "readlink(\"/proc/self/ns/afnet\") exceeded maximum path length: %d >= %ld",
			err, path_size);
		err = -1;
		goto out;
	}
	path[err] = '\0';

	if (sscanf(path, "afnet:[%lu]", &inode) != 1) {
		perror("sscanf");
		err = -1;
		goto out;
	}

	initialized = true;
	*me = inode;
	err = 0;
out:
	free(path);
	return err;
}

static struct inode_cache **lookup_node(ino_t inode)
{
	struct inode_cache **node;

	node = cache + (inode & 63);
	while (*node && node[0]->inode != inode)
		node = &node[0]->next;

	return node;
}

static void fill_cache(void)
{
	struct dirent *ent;
	ino_t me;
	DIR *dir;

	if (self_inode(&me))
		return;

	dir = opendir(AFNETNS_RUN_DIR);
	if (!dir)
		return;

	errno = 0;
	while ((ent = readdir(dir))) {
		struct inode_cache **node;
		struct stat buf;
		ino_t inode;
		bool self;
		char *end;
		int fd;

		if (!strcmp(ent->d_name, ".") ||
		    !strcmp(ent->d_name, ".."))
			continue;

		fd = dirfd(dir);
		if (fd < 0) {
			perror("dirfd");
			continue;
		}

		if (fstatat(fd, ent->d_name, &buf, 0)) {
			perror("fstatat");
			continue;
		}

		inode = buf.st_ino;
		self = me == inode;

		node = lookup_node(inode);
		if (*node)
			continue;

		*node = malloc(sizeof(**node)
			       + strlen(ent->d_name)
			       + (self ? strlen(",self") : 0)
			       + 1);
		if (!*node)
			continue;

		node[0]->next = NULL;
		node[0]->inode = inode;
		end  = stpcpy(node[0]->name, ent->d_name);
		if (self)
			strcpy(end, ",self");

		errno = 0;
	}

	if (errno)
		perror("readdir");

	if (closedir(dir))
		perror("closedir");
}

static char *lookup_cache(ino_t inode)
{
	struct inode_cache **node;
	bool self;
	ino_t me;

	node = lookup_node(inode);
	if (*node)
		return node[0]->name;

	if (self_inode(&me))
		return NULL;

	self = me == inode;

	*node = malloc(sizeof(**node) + ULONG_CHARS + strlen("afnet:[]") + 1 +
		       (self ? strlen(",self") : 0));
	if (!*node)
		return NULL;

	if (sprintf(node[0]->name, "afnet:[%lu]%s", inode, self ? ",self" : "") < 0) {
		free(*node);
		*node = NULL;
		return NULL;
	}

	node[0]->next = NULL;
	node[0]->inode = inode;
	return node[0]->name;
}

char *afnetns_lookup_name(ino_t inode)
{
	static bool initialized = false;

	if (!initialized) {
		fill_cache();
		initialized = true;
	}

	return lookup_cache(inode);
}

int afnetns_open(const char *name)
{
	int ns;
	char *path;

	ns = asprintf(&path, "%s/%s", AFNETNS_RUN_DIR, name);
	if (ns < 0) {
		perror("asprintf");
		return ns;
	};

	ns = open(path, O_RDONLY | O_CLOEXEC);
	if (ns < 0) {
		fprintf(stderr, "Cannot open afnet namespace \"%s\": %s\n",
			name, strerror(errno));
	}

	free(path);
	return ns;
}
