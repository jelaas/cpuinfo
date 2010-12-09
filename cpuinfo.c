/*
 * File: cpuinfo.c
 * Implements: script friendly cpu information retrieval
 *
 * Copyright: Jens Låås, 2010
 * Copyright license: According to GPL, see file COPYING in this directory.
 *
 */

/*

 cpuinfo [-n cpu] [-aw] [-p S] [-s S] [key]

 freq:N
 node:N
 siblings:L
 irq:N

 numahit:N
 pktcount:N

 clocksource:S

/sys/devices/system/cpu/online
/sys/devices/system/cpu/cpu0/cache/index0/size
/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq
/sys/devices/system/cpu/cpu0/cpufreq/related_cpus
/sys/devices/system/cpu/cpu0/cpufreq/bios_limit
/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

/sys/devices/system/cpu/cpu0/topology/core_id
/sys/devices/system/cpu/cpu0/topology/core_siblings
/sys/devices/system/cpu/cpu0/topology/thread_siblings

/sys/devices/system/cpu/cpu4/node1

/sys/devices/system/node/
/sys/devices/system/node/node0/numastat
/sys/devices/system/node/node0/meminfo

/sys/devices/system/clocksource/clocksource0/current_clocksource

/proc/net/softnet_stat

/proc/net/stat/rt_cache
/proc/net/stat/arp_cache

/proc/cpuinfo

/proc/interrupts

/proc/stat
Lines of: "cpu0 31 0 178 17037710 292 0 1 0 0 0"
usertime, nicetime, systemtime, idletasktime, iowait, irqtime, softirqtime, stealtime, guesttime

 */
#ifndef VERSION
#define VERSION "apa"
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <stdio.h>
#include <string.h>


#include "jelist.h"
#include "jelopt.h"


struct node {
	int n;
	/* /sys/devices/system/node/node0/numastat */
	uint64_t numa_hit, numa_miss, numa_foreign, interleave_hit;
	uint64_t local_node, other_node;
	
	/* /sys/devices/system/node/node0/meminfo */
	uint64_t memtotal, memused;
};

struct cpu {
	int n;
	uint64_t irqs;
	struct node *node;
	struct jlhead *props; /* list of struct cpuprop */
};

struct cpuprop {
	const char *key, *value;
};

struct {
	int debug;
	int listall, nowhite, list;
	int cpu;
	char *prefix, *suffix;
	struct jlhead *keys;
} conf;

static char *nowhite(const char *s)
{
	char *w, *p;
	if(!conf.nowhite) return strdup(s);
	w = strdup(s);
	for(p=w;*p;p++)
		if(strchr(" \t\n\r", *p))
			*p='_';
	return w;
}

static int strpfx(const char *line, const char *pfx)
{
	if(strncmp(line, pfx, strlen(pfx))==0)
		return 1;
	return 0;
}

static char *strend(const char *p)
{
	const char *end;
	end = strchr(p, '\n');
	if(!end) return strdup(p);
	return strndup(p, end-p);
}

static int keycmp(const struct jlhead *keys, const char *key)
{
        char *k;

        jl_foreach(keys, k) {
                if(!strcmp(k, key)) return 1;
	}
        return 0;
}


static struct cpuprop *cpuprop_new(const char *key, const char *value)
{
        struct cpuprop *prop;
	
        if(conf.debug) printf("new prop %s = %s\n", key, value);
        prop = malloc(sizeof(struct cpuprop));
        prop->key = key;
        prop->value = value;
        return prop;
}

static int cpu_set_str(struct cpu *cpu, const char *key, const char *value)
{
	return jl_append(cpu->props, cpuprop_new(key, value));
}

static int cpu_set(struct cpu *cpu, const char *key, uint64_t value)
{
	char buf[64];
	sprintf(buf, "%llu", value);
	return jl_append(cpu->props, cpuprop_new(key, strdup(buf)));
}

static struct cpu *cpu_new(int n)
{
	struct cpu *cpu;

	cpu = malloc(sizeof(struct cpu));
	cpu->n = n;
	cpu->irqs = 0;
	cpu->node = NULL;
	cpu->props = jl_new();
	return cpu;
}

static struct node *node_new(int n)
{
	struct node *node;

	node = malloc(sizeof(struct node));
	memset(node, 0, sizeof(struct node));
	node->n = n;
	return node;
}

static int getfile(const char *fn, char *buf, int bufsize)
{
	int fd, rc, tot=0;
	fd = open(fn, O_RDONLY);
	if(fd == -1) {
		return -1;
	}
	while(bufsize>0) {
		rc = read(fd, buf, bufsize);
		if(rc > 1) {
			buf[rc] = 0;
			tot += rc;
			buf += rc;
			bufsize -= rc;
		} else
			break;
	}
	close(fd);
	return tot;
}

static const char *strv(const char *stack, const char *needle)
{
	const char *p;

	p = strstr(stack, needle);
	if(p) {
		p += strlen(needle);
	}
	return p;
}

static int node_scan(struct jlhead *nodes)
{
	int n;
	char fn[256], buf[256];
	int rc;
	struct node *node;
	const char *p;

	for(n=0;;n++) {
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/node/node%d/numastat",
			 n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc <= 0) break;
		
		node = node_new(n);
		jl_append(nodes, node);
		
		p = strv(buf, "numa_hit ");
		if(p) node->numa_hit = strtoull(p, NULL, 10);
		p = strv(buf, "numa_miss ");
		if(p) node->numa_miss = strtoull(p, NULL, 10);
		p = strv(buf, "numa_foreign ");
		if(p) node->numa_foreign = strtoull(p, NULL, 10);
		p = strv(buf, "interleave_hit ");
		if(p) node->interleave_hit = strtoull(p, NULL, 10);
		p = strv(buf, "local_node ");
		if(p) node->local_node = strtoull(p, NULL, 10);
		p = strv(buf, "other_node ");
		if(p) node->other_node = strtoull(p, NULL, 10);
		
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/node/node%d/meminfo",
			 n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc <= 0) continue;
		p = strv(buf, "MemTotal: ");
		if(p) node->memtotal = strtoull(p, NULL, 10);
		p = strv(buf, "MemUsed: ");
		if(p) node->memused = strtoull(p, NULL, 10);
	}
	return 0;
}

static int cpu_assign_node(struct jlhead *cpus, const struct jlhead *nodes)
{
	int found, n;
	char fn[256];
	struct stat statb;
	struct cpu *cpu;
	struct node *node;

	jl_foreach(cpus, cpu) {
		found = -1;
		for(n=0;n<nodes->len;n++) {
			snprintf(fn, sizeof(fn),
				 "/sys/devices/system/cpu/cpu%d/node%d",
				 cpu->n,
				 n);
			if(stat(fn, &statb)==0) {
				found = n;
				break;
			}
		}
		if(found == -1) continue;
		
		jl_foreach(nodes, node) {
			if(node->n == found)
				cpu->node = node;
		}
	}
	return 0;
}

static int cpu_scan(struct jlhead *cpus, const struct jlhead *nodes)
{
	int n;
	char fn[256];
	int rc;
	struct stat statb;

	for(n=0;;n++) {
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d",
			 n);
		rc = stat(fn, &statb);
		if(rc) break;
		
		jl_append(cpus, cpu_new(n));
	}
	return 0;
}

int cpu_assign_props(struct jlhead *cpus)
{
	struct cpu *cpu;
	char fn[256], buf[256];
	int rc;

	jl_foreach(cpus, cpu) {
		if(cpu->node) {
			cpu_set(cpu, "node", cpu->node->n);
			cpu_set(cpu, "numa_hit", cpu->node->numa_hit);
			cpu_set(cpu, "numa_miss", cpu->node->numa_miss);
			cpu_set(cpu, "memtotal", cpu->node->memtotal);
			cpu_set(cpu, "memused", cpu->node->memused);
		}

		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_cur_freq",
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set(cpu, "cur_freq", strtoull(buf, NULL, 10));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq",
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set(cpu, "max_freq", strtoull(buf, NULL, 10));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/topology/physical_package_id",
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set(cpu, "physical_package_id", strtoull(buf, NULL, 10));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/topology/core_siblings_list",
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set_str(cpu, "core_siblings_list", strend(buf));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list",
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set_str(cpu, "thread_siblings_list", strend(buf));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/cache/index0/size", 
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set_str(cpu, "cache0_size", strend(buf));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/cache/index0/type", 
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set_str(cpu, "cache0_type", strend(buf));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/cache/index1/size", 
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set_str(cpu, "cache1_size", strend(buf));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/cache/index1/type", 
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set_str(cpu, "cache1_type", strend(buf));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/cache/index2/size", 
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set_str(cpu, "cache2_size", strend(buf));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/cache/index2/type", 
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set_str(cpu, "cache2_type", strend(buf));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/cache/index3/size", 
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set_str(cpu, "cache3_size", strend(buf));
		}
		snprintf(fn, sizeof(fn),
			 "/sys/devices/system/cpu/cpu%d/cache/index3/type", 
			 cpu->n);
		rc = getfile(fn, buf, sizeof(buf)-1);
		if(rc>0) {
			cpu_set_str(cpu, "cache3_type", strend(buf));
		}

	}
	return 0;
}


static int scan_softnet(struct jlhead *cpus)
{
	struct cpu *cpu;
	char buf[20480], *p;
	int rc;

	rc = getfile("/proc/net/softnet_stat", buf, sizeof(buf)-1);
	if(rc<=0) return 1;

	p = buf;
	jl_foreach(cpus, cpu) {
		cpu_set(cpu, "softnet_stat", strtoull(p, NULL, 16));
		p = strchr(p, '\n');
		if(!p) break;
		p++;
	}
	return 0;
}

static int scan_rt_cache(struct jlhead *cpus)
{
	struct cpu *cpu;
	char buf[20480], *p, *v;
	int rc;

	rc = getfile("/proc/net/stat/rt_cache", buf, sizeof(buf)-1);
	if(rc<=0) return 1;

	p = buf;
	p = strchr(p, '\n');
	if(p) p++;
	jl_foreach(cpus, cpu) {
		cpu_set(cpu, "rt_cache_entries", strtoull(p, NULL, 16));
		v = strchr(p, ' ');
		if(v) {
			while(*v == ' ') v++;
			cpu_set(cpu, "rt_cache_in_hit",
				strtoull(v, NULL, 16));
			v = strchr(v, ' ');
			if(v)
				cpu_set(cpu, "rt_cache_in_slow_tot",
					strtoull(v+1, NULL, 16));
		}
		p = strchr(p, '\n');
		if(!p) break;
		p++;
	}
	return 0;
}

static int scan_cpuinfo(struct jlhead *cpus)
{
	struct cpu *cpu = NULL;
	char buf[102400], *p, *v;
	int rc;
	
	rc = getfile("/proc/cpuinfo", buf, sizeof(buf)-1);
	if(rc<=0) return 1;
	
	p = buf;
	
	while(1) {
		if(strpfx(p, "processor")) {
			if(cpu) cpu = jl_next(cpu);
			else
				cpu = jl_head_first(cpus);
		}
		if(cpu) {
			if(strpfx(p, "model name")) {
				v = strchr(p, ':');
				if(v) {
					v+=2;
					cpu_set_str(cpu, "model_name",
						    strend(v));
				}
			}
			if(strpfx(p, "flags")) {
				v = strchr(p, ':');
				if(v) {
					v+=2;
					cpu_set_str(cpu, "flags",
						    strend(v));
				}
			}
			if(strpfx(p, "cpu cores")) {
				v = strchr(p, ':');
				if(v) {
					v+=2;
					cpu_set_str(cpu, "cpu_cores",
						    strend(v));
				}
			}
			if(strpfx(p, "vendor_id")) {
				v = strchr(p, ':');
				if(v) {
					v+=2;
					cpu_set_str(cpu, "vendor_id",
						    strend(v));
				}
			}
			if(strpfx(p, "model\t")) {
				v = strchr(p, ':');
				if(v) {
					v+=2;
					cpu_set_str(cpu, "model",
						    strend(v));
				}
			}
			if(strpfx(p, "cpu family")) {
				v = strchr(p, ':');
				if(v) {
					v+=2;
					cpu_set_str(cpu, "cpu_family",
						    strend(v));
				}
			}

		}
		p = strchr(p, '\n');
		if(!p) break;
		p++;
	}
	return 0;
}

static struct cpu *cpu_get(struct jlhead *cpus, int n)
{
	struct cpu *cpu;
	jl_foreach(cpus, cpu) {
		if(cpu->n == n)
			return cpu;
	}
	return NULL;
}

static int scan_cpustat(struct jlhead *cpus)
{
	struct cpu *cpu = NULL;
	char buf[102400], *p;
	int rc;
	
	rc = getfile("/proc/stat", buf, sizeof(buf)-1);
	if(rc<=0) return 1;
	
	p = buf;
	
	while(1) {
		if(strpfx(p, "cpu ")) {
			goto nextline;
		}

		if(strpfx(p, "cpu")) {
			cpu = cpu_get(cpus, atoi(p+3));
			if(cpu) {
				p = strchr(p, ' ');
				if(!p) break;
				p++;
				cpu_set(cpu, "user", strtoull(p, NULL, 10));
				p = strchr(p, ' ');
				if(!p) break;
				p++;
				cpu_set(cpu, "nice", strtoull(p, NULL, 10));
				p = strchr(p, ' ');
				if(!p) break;
				p++;
				cpu_set(cpu, "system", strtoull(p, NULL, 10));
				p = strchr(p, ' ');
				if(!p) break;
				p++;
				cpu_set(cpu, "idle", strtoull(p, NULL, 10));
				p = strchr(p, ' ');
				if(!p) break;
				p++;
				cpu_set(cpu, "iowait", strtoull(p, NULL, 10));
				p = strchr(p, ' ');
				if(!p) break;
				p++;
				cpu_set(cpu, "irqtime", strtoull(p, NULL, 10));
				p = strchr(p, ' ');
				if(!p) break;
				p++;
				cpu_set(cpu, "softirqtime", strtoull(p, NULL, 10));
				p = strchr(p, ' ');
				if(!p) break;
				p++;
				cpu_set(cpu, "steal", strtoull(p, NULL, 10));
				p = strchr(p, ' ');
				if(!p) goto nextline;
				p++;
				cpu_set(cpu, "guest", strtoull(p, NULL, 10));
			}
		}
	nextline:
		p = strchr(p, '\n');
		if(!p) break;
		p++;
	}
	return 0;
}

static int scan_interrupts(struct jlhead *cpus)
{
	struct cpu *cpu;
	char buf[20480], *p;
	int rc;

	rc = getfile("/proc/interrupts", buf, sizeof(buf)-1);
	if(rc<=0) return 1;

	p = buf;

	while(1) {
		p = strchr(p, ':');
		if(!p) break;
		p++;
		
		jl_foreach(cpus, cpu) {
			while(*p == ' ') p++;
			cpu->irqs += strtoull(p, NULL, 10);
			while(*p != ' ') p++;
		}
		p = strchr(p, '\n');
		if(!p) break;
	}

	jl_foreach(cpus, cpu) {
		cpu_set(cpu, "irqs", cpu->irqs);
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct jlhead *cpus, *nodes;
	struct cpu *cpu;
	struct cpuprop *prop;
	int rc, i, err = 0;

	cpus = jl_new();
	nodes = jl_new();

	conf.list = 1;
        conf.prefix = "";
        conf.suffix = "";
	conf.keys = jl_new();

	if(jelopt(argv, 'h', "help", NULL, &err)) {
		printf("cpuinfo [-aw] [-ps] [-n CPU] [key]\n"
                       "version " VERSION "\n"
                       " -a --all         Output all keys found.\n"
		       " -n --cpu         Output info for this cpu only.\n"
                       " -w --nowhite     Do not output whitespace in values.\n"
                       " -p --prefix S    Prefix values with string S.\n"
		              " -s --suffix S    Append string S to values.\n"
                       " --debug\n"
                       "\n"
                       "key = freq|node ...\n");
                exit(0);
        }

	if(jelopt(argv, 0, "debug", NULL, &err))
		conf.debug = 1;

	if(jelopt(argv, 'a', "all", NULL, &err))
                conf.listall = 1;
        if(jelopt(argv, 'w', "nowhite", NULL, &err))
                conf.nowhite = 1;
        if(jelopt(argv, 'p', "prefix", &conf.prefix, &err))
		;
        if(jelopt(argv, 's', "suffix", &conf.suffix, &err))
                ;
	if(jelopt_int(argv, 'n', "cpu",
		      &conf.cpu, &err))
		conf.list = 0;
	
        argc = jelopt_final(argv, &err);
	
	for(i=1;i<argc;i++)
                jl_append(conf.keys, argv[i]);
	
        if(argc >= 2) {
                /* -a and key are mutually exclusive: key overrides */
		conf.listall = 0;
        }
	
	/* fetch node info */
	node_scan(nodes);

	/* create list of all cpus */
	cpu_scan(cpus, nodes);
	
	/* assign nodes */
	cpu_assign_node(cpus, nodes);

	/* create properties for each cpu */
	cpu_assign_props(cpus);

	/* softnet_stat */
	scan_softnet(cpus);

	/* interrupts */
	scan_interrupts(cpus);

	scan_cpuinfo(cpus);

	scan_rt_cache(cpus);

	scan_cpustat(cpus);

	/* display */
	jl_foreach(cpus, cpu) {
		if(conf.list) {
			if(!conf.keys->len && !conf.listall)
				printf("%d", cpu->n);
		} else {
			if(conf.cpu != cpu->n)
				continue;
		}
		
		if(conf.keys->len)
			jl_foreach(cpu->props, prop) {
				if(keycmp(conf.keys, prop->key)) {
					if(conf.list)
						printf("%d:", cpu->n);
					if(conf.keys->len > 1)
						printf("%s=", prop->key);
					printf("%s%s%s\n",
					       conf.prefix,
					       nowhite(prop->value),
					       conf.suffix);
					rc=0;
				}
			}
		if(conf.listall) {
			jl_foreach(cpu->props, prop) {
				if(conf.list)
					printf("%d:", cpu->n);
				printf("%s=%s%s%s\n",
				       prop->key,
				       conf.prefix,
				       nowhite(prop->value),
				       conf.suffix);
			}
			rc=0;
		}
		if(conf.list && !conf.listall) printf("%s", conf.keys->len?"":"\n");
	}
	exit(0);
}
