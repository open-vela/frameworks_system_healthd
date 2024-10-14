/*
 * Copyright (C) 2020 Xiaomi Corperation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <ctype.h>
#include <debug.h>
#include <dirent.h>
#include <fcntl.h>
#include <nuttx/power/battery_charger.h>
#include <nuttx/power/battery_gauge.h>
#include <nuttx/power/battery_ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>

#include <system/state.h>
#include <uORB/uORB.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CHARGE_DIR_PATH "/dev/charge/"
#define CHARGE_DIR_LENTH 12

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* The structure contains context of charge device advertising as a topic */

struct charge_data {
    int status;
    int health;
    int online;
    b16_t voltage;
    b16_t current;
    b16_t capacity;
    int cellvoltage;
    b8_t temp;
    int coulombs;
};

struct charge_manager {
    struct charge_data c_data; /* charge data */
    const struct orb_metadata* meta; /* Object metedata */
    unsigned int qsize; /* Queue size of advertising topic */
    int instance; /* Instance of advertising topic */
    int epollfd; /* File descriptor of epoll instance */
    int cnt; /* The number of charge device */
    int sfd[5]; /* File descriptor of charge device node */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_should_exit = false;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int open_charge(struct charge_manager* manager, const char* devname)
{
    struct epoll_event ev;
    int sfd;
    int ret;

    /* Open charge device node, eg:/dev/charge/batt_gauge */

    sfd = open(devname, O_RDONLY | O_CLOEXEC);
    if (sfd < 0) {
        ret = -errno;
        baterr("err open device:%s ret:%d\n", devname, ret);
        return ret;
    }

    /* Monitor charge node: if there is POLLIN event,
     * we will read charge_event by sfd.
     */

    ev.events = POLLIN;
    ev.data.fd = sfd;
    ret = epoll_ctl(manager->epollfd, EPOLL_CTL_ADD, sfd, &ev);
    if (ret < 0) {
        ret = -errno;
        baterr("err add epoll:%s ret:%d\n", devname, ret);
        close(sfd);
        return ret;
    }

    manager->sfd[manager->cnt++] = sfd;

    return ret;
}

static void close_charge(struct charge_manager* manager)
{
    int sfd;
    int idx = 0;

    for (; idx < manager->cnt; idx++) {
        sfd = manager->sfd[idx];
        epoll_ctl(manager->epollfd, EPOLL_CTL_DEL, sfd, NULL);
        close(sfd);
    }

    epoll_close(manager->epollfd);
}

static int filter_dirent(const struct dirent* dir)
{
    if ((dir->d_name[0] == '.' && dir->d_name[1] == '\0')
        || (dir->d_name[0] == '.' && dir->d_name[2] == '\0')) {
        return false;
    }

    return true;
}

static int scan_charge(struct charge_manager* manager, const char* dirname)
{
    struct dirent** dirlist;
    char devname[PATH_MAX];
    char* filename;
    int ret;

    ret = scandir(dirname, &dirlist, filter_dirent, NULL);
    if (ret < 0)
        return ret;

    manager->epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (manager->epollfd == -1) {
        while (ret--) {
            free(dirlist[ret]);
        }
        ret = -errno;
        goto poll_err;
    }

    strcpy(devname, dirname);
    filename = devname + strlen(devname);

    while (ret--) {
        strcpy(filename, dirlist[ret]->d_name);
        open_charge(manager, devname);
        free(dirlist[ret]);
    }
    free(dirlist);
    return 0;

poll_err:
    free(dirlist);
    return ret;
}

static void exit_handler(int signo)
{
    g_should_exit = true;
}

static int read_charge_data(int sfd, struct battery_state* data,
    struct charge_manager* manager)
{
    int ret;
    uint32_t mask;

    ret = read(sfd, &mask, sizeof(uint32_t));
    if (ret < 0) {
        baterr("read mask err:%d\n", ret);
        return ret;
    }

    while (mask != 0) {

        if (mask & BATTERY_STATE_CHANGED) {
            ret = ioctl(sfd, BATIOC_STATE,
                (unsigned long)(uintptr_t)&manager->c_data.status);
            if (ret < 0) {
                baterr("ioctl(STATE) err:%d\n", ret);
                return ret;
            }

            if (manager->c_data.status == BATTERY_CHARGING || manager->c_data.status == BATTERY_FULL) {
                data->state = 1;
            } else if (manager->c_data.status == BATTERY_FAULT) {
                data->state = 2;
            } else {
                data->state = 0;
            }

            mask &= ~BATTERY_STATE_CHANGED;
            continue;
        } else if (mask & BATTERY_HEALTH_CHANGED) {
            ret = ioctl(sfd, BATIOC_HEALTH,
                (unsigned long)(uintptr_t)&manager->c_data.health);
            if (ret < 0) {
                baterr("ioctl(HEALTH) err:%d\n", ret);
                return ret;
            }

            mask &= ~BATTERY_HEALTH_CHANGED;
            continue;
        } else if (mask & BATTERY_ONLINE_CHANGED) {
            ret = ioctl(sfd, BATIOC_ONLINE,
                (unsigned long)(uintptr_t)&manager->c_data.online);
            if (ret < 0) {
                baterr("ioctl(ONLINE) err:%d\n", ret);
                return ret;
            }
            data->online = manager->c_data.online;
            mask &= ~BATTERY_ONLINE_CHANGED;
            continue;
        } else if (mask & BATTERY_VOLTAGE_CHANGED) {
            ret = ioctl(sfd, BATIOC_VOLTAGE,
                (unsigned long)(uintptr_t)&manager->c_data.voltage);
            if (ret < 0) {
                baterr("ioctl(VOLTAGE) err:%d\n", ret);
                return ret;
            }
            data->voltage = manager->c_data.voltage;
            mask &= ~BATTERY_VOLTAGE_CHANGED;
        } else if (mask & BATTERY_CURRENT_CHANGED) {
            ret = ioctl(sfd, BATIOC_CURRENT,
                (unsigned long)(uintptr_t)&manager->c_data.current);
            if (ret < 0) {
                baterr("ioctl(CURRENT) err:%d\n", ret);
                return ret;
            }

            data->curr = manager->c_data.current;
            mask &= ~BATTERY_CURRENT_CHANGED;
            continue;
        } else if (mask & BATTERY_CAPACITY_CHANGED) {
            ret = ioctl(sfd, BATIOC_CAPACITY,
                (unsigned long)(uintptr_t)&manager->c_data.capacity);
            if (ret < 0) {
                baterr("ioctl(CAPACITY) err:%d\n", ret);
                return ret;
            }

            data->level = manager->c_data.capacity;
            mask &= ~BATTERY_CAPACITY_CHANGED;
            continue;
        } else if (mask & BATTERY_CELLVOLTAGE_CHANGED) {
            ret = ioctl(sfd, BATIOC_CELLVOLTAGE,
                (unsigned long)(uintptr_t)&manager->c_data.cellvoltage);
            if (ret < 0) {
                baterr("ioctl(CELLVOLTAGE) err:%d\n", ret);
                return ret;
            }

            mask &= ~BATTERY_CELLVOLTAGE_CHANGED;
            continue;
        } else if (mask & BATTERY_TEMPERATURE_CHANGED) {
            ret = ioctl(sfd, BATIOC_TEMPERATURE,
                (unsigned long)(uintptr_t)&manager->c_data.temp);
            if (ret < 0) {
                baterr("ioctl(TEMPERATURE) err:%d\n", ret);
                return ret;
            }

            data->temp = manager->c_data.temp;
            mask &= ~BATTERY_TEMPERATURE_CHANGED;
            continue;
        } else if (mask & BATTERY_COULOMBS_CHANGED) {
            ret = ioctl(sfd, BATIOC_COULOMBS,
                (unsigned long)(uintptr_t)&manager->c_data.coulombs);
            if (ret < 0) {
                baterr("ioctl(COULOMBS) err:%d\n", ret);
                return ret;
            }

            mask &= ~BATTERY_COULOMBS_CHANGED;
            continue;
        } else {
            baterr("read mask err:%" PRIi32 "\n", mask);
            ret = -ENOTTY;
        }
    }

    return ret;
}

static void poll_charge(struct charge_manager* manager, struct battery_state* data)
{
    struct epoll_event events[manager->cnt];
    int idx;
    int ret;

    while (!g_should_exit) {
        idx = epoll_wait(manager->epollfd, events, manager->cnt, -1);
        while (idx-- > 0) {
            if (events[idx].events & POLLIN) {
                ret = read_charge_data(events[idx].data.fd, data, manager);
                if (ret < 0) {
                    baterr("read charge data err\n");
                }

                if (orb_publish_auto(ORB_ID(battery_state), NULL, data, NULL) < 0) {
                    baterr("battery state publish err\n");
                }

                baterr("healthd state:%d level:%d temp:%d online:%d\n", data->state, data->level, data->temp, data->online);
            }
        }
    }
}

void init_charge_uorb_data(struct battery_state* data)
{
    data->state = 0;
    data->level = 0;
    data->temp = 250;
}

int main(int argc, char* argv[])
{
    struct charge_manager manager = {};
    struct battery_state data = {};
    int ret;

    g_should_exit = false;
    if (signal(SIGINT, exit_handler) == SIG_ERR) {
        baterr("err setup handler:%s\n", strerror(errno));
        return -errno;
    }

    init_charge_uorb_data(&data);

    /* Scan dir "/dev/charge/" and do the following */

    ret = scan_charge(&manager, CHARGE_DIR_PATH);
    if (ret < 0)
        return ret;

    /* Monitor all fd, read and process event */

    poll_charge(&manager, &data);

    /* It should never come here unless ctrl+c exits */

    close_charge(&manager);
    return 0;
}
