/* Hacky wonky incomplete af decompilation by snoole k in 1 day i have no idea what im doing */

#include "config.h"
#include "launchd.h"

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <sys/ucred.h>
#include <sys/fcntl.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/sysctl.h>
#include <sys/sockio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/kern_event.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet6/nd6.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <paths.h>
#include <pwd.h>
#include <grp.h>
#include <ttyent.h>
#include <dlfcn.h>
#include <dirent.h>
#include <string.h>
#include <setjmp.h>
#include <spawn.h>
#include <sched.h>
#include <pthread.h>
#include <util.h>
#include <os/assumes.h>

#if HAVE_LIBAUDITD
#include <bsm/auditd_lib.h>
#include <bsm/audit_session.h>
#endif

#include "bootstrap.h"
#include "vproc.h"
#include "vproc_priv.h"
#include "vproc_internal.h"
#include "launch.h"
#include "launch_internal.h"

#include "runtime.h"
#include "core.h"
#include "ipc.h"

xpc_object_t launchPlist;
xpc_object_t bootstrapPlist;
xpc_object_t uncachedServices;
pid_t launchd_pid;
bool launchd_ramdisk;

xpc_object_t xpc_dictionary_from_path(const char *path) {
    int ret = 0;
    if (path) {
        int fd = open(path, 0);
        if (fd != -1) {
            struct stat sb;
            off_t
            int fail = fstat(fd, &sb);
            if (!fail && (wtf1 == 0)) {
                if (wtf2 & 0x12) {
                    
                } else {
                    ret = xpc_dictionary_from_fd(fd, &sb)
                }
            }
            if (close(fd) == -1) {
                /* error */
            }
        }
    }
    return ret;
}

xpc_object_t launchd_bool_from_plist(const char *key) {
    bool boolValue;
    xpc_object_t value;
    if (bootstrapPlist) {
        value = xpc_dictionary_get_value(bootstrapPlist, key);
        if ((value) && (xpc_get_type(value) == XPC_TYPE_BOOL)) {
            boolValue = xpc_bool_get_value(value);
        } else {
            boolValue = xpc_dictionary_get_bool(launchPlist, key);
        }
    } else {
        boolValue = xpc_dictionary_get_bool(launchPlist, key);
    }
    return boolValue;
}


xpc_object_t launchd_value_from_plist(const char *key) {
    xpc_object_t value = 0;
    if (bootstrapPlist) {
        value = xpc_dictionary_get_value(bootstrapPlist, key);
    }
    /* if the key is not on the bootstrap plist, maybe it's on the embedded plist? */
    if (!value && launchPlist) {
        value = xpc_dictionary_get_value(launchPlist, key);
    }
    return value;
}

bool launchd_is_ramdisk(void) {
    bool is_ramdisk = false;
    const char *bootargs;
    size_t bootarglen = sysctlbyname_on_crack("kern.bootargs", &bootargs);
    if (bootarglen) {
        if (strnstr(bootargs, "rd=md0", bootarglen)) {
            is_ramdisk = true;
        }
    }
    free(bootargs);
    return is_ramdisk;
}

void do_launchd_init(void) {
    /* Find our embedded plist */
    xpc_object_t plist = lookupPlist(0, "__TEXT", "__bs_plist");
    launchPlist = plist;
    if (plist) {
        /* load the bootstrap plist as well */
        bootstrapPlist = xpc_dictionary_from_path("/com.apple.xpc.launchd.bootstrap.plist");
        uncachedServices = launchd_value_from_plist("UncachedServices");
        if (!uncachedServices) {
            uncachedServices = launchd_value_from_plist("uncached-services");
            if (!uncachedServices) {
                uncachedServices = xpc_dictionary_create(NULL, NULL, 0);
            }
        }
        /* if no uncached services are listed, create an empty dict */
        if (xpc_get_type(uncachedServices) != XPC_TYPE_DICTIONARY) {
            uncachedServices = xpc_dictionary_create(NULL, NULL, 0);
        }
        xpc_object_t limits = xpc_dictionary_from_path("/com.apple.xpc.launchd.limits.plist");
        if (limits) {
            xpc_dictionary_apply(limits, ^bool(const char * _Nonnull key, xpc_object_t  _Nonnull value) {
                if (xpc_get_type(value) != XPC_TYPE_INT64) {
                    if (xpc_get_type(value) != XPC_TYPE_ARRAY) {
                        return false;
                    }
                    if (xpc_array_get_count(value) != 1) {
                        if (xpc_array_get_count(value) != 2) { return false };
                        xpc_object_t limitone = xpc_array_get_value(value, 0);
                        xpc_object_t limittwo = xpc_array_get_value(value, 1);
                        if (xpc_get_type(limitone) != XPC_TYPE_INT64) {return false;};
                        if (xpc_get_type(limittwo) != XPC_TYPE_INT64) {return false;};
                        //return finishlater(key, xpc_int64_get_value(limitone), xpc_int64_get_value(limittwo));
                    }
                    value = xpc_array_get_value(value, 0);
                    if (xpc_get_type(value) != XPC_TYPE_INT64) {
                        return false;
                    }
                }
                int64_t val64 = xpc_int64_get_value(value);
                //return finishlatr(key, val64, val64);
            })
        }
        if (getpid() == 1) {
            pid1_magic = true;
        } else {
            pid1_magic = false;
        }
        struct stat statbuf;
        if (stat("/AppleInternal", &statbuf) == 0) {
            launchd_apple_internal = 1;
        } else {
            launchd_apple_internal = 0;
        }
        launchd_ramdisk = is_ramdisk();
        const char *bootargs;
        size_t bootarglen = sysctlbyname_on_crack("kern.bootargs", &bootargs);
        if (bootarglen) {
            /* finish later */
        }
    }
}

int main(int argc, char *const *argv) {
    if (isatty(1) != 0) {
        fprintf(stdout, "%s cannot be run directly.\n", getprogname());
        exit(EXIT_FAILURE);
    }
    
    panic_init(mach_host_self());

    test_of_openfd(_PATH_DEVNULL, O_RDONLY, STDIN_FILENO);
    test_of_openfd(_PATH_DEVNULL, O_WRONLY, STDOUT_FILENO);
    test_of_openfd(_PATH_DEVNULL, O_RDWR, STDERR_FILENO);

    do_launchd_init();
    handle_pid1_crashes_separately();

    /* finish later */
    return 0;
}