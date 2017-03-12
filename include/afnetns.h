#pragma once

#define AFNETNS_RUN_DIR "/var/run/afnetns"

int afnetns_open(const char *name);
char *afnetns_lookup_name(ino_t inode);
