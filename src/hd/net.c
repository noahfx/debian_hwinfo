#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t
#include <linux/if.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>
#include <linux/if_arp.h>

#include "hd.h"
#include "hd_int.h"
#include "net.h"

/**
 * @defgroup NETint Network devices
 * @ingroup libhdDEVint
 * @brief Network device scan functions
 *
 * Gather network interface info
 *
 * @{
 */

static void get_driverinfo(hd_data_t *hd_data, hd_t *hd);
static void get_linkstate(hd_data_t *hd_data, hd_t *hd);
static void add_xpnet(hd_data_t *hdata);
static void add_uml(hd_data_t *hdata);
static void add_kma(hd_data_t *hdata);
static void add_if_name(hd_t *hd_card, hd_t *hd);

/*
 * This is independent of the other scans.
 */

void hd_scan_net(hd_data_t *hd_data)
{
  unsigned u;
  int if_type, if_carrier;
  hd_t *hd, *hd_card;
  char *s, *t, *hw_addr;
  hd_res_t *res, *res1, *res2;
  uint64_t ul0;
  str_list_t *sf_class, *sf_class_e;
  char *sf_cdev = NULL, *sf_dev = NULL;
  char *sf_drv_name, *sf_drv;

  if(!hd_probe_feature(hd_data, pr_net)) return;

  hd_data->module = mod_net;

  /* some clean-up */
  remove_hd_entries(hd_data);
  hd_data->net = free_str_list(hd_data->net);

  PROGRESS(1, 0, "get network data");

  sf_class = read_dir("/sys/class/net", 'l');
  if(!sf_class) sf_class = read_dir("/sys/class/net", 'd');

  if(!sf_class) {
    ADD2LOG("sysfs: no such class: net\n");
    return;
  }

  for(sf_class_e = sf_class; sf_class_e; sf_class_e = sf_class_e->next) {
    str_printf(&sf_cdev, 0, "/sys/class/net/%s", sf_class_e->str);

    hd_card = NULL;

    ADD2LOG(
      "  net interface: name = %s, path = %s\n",
      sf_class_e->str,
      hd_sysfs_id(sf_cdev)
    );

    if_type = -1;
    if(hd_attr_uint(get_sysfs_attr_by_path(sf_cdev, "type"), &ul0, 0)) {
      if_type = ul0;
      ADD2LOG("    type = %d\n", if_type);
    }

    if_carrier = -1;
    if(hd_attr_uint(get_sysfs_attr_by_path(sf_cdev, "carrier"), &ul0, 0)) {
      if_carrier = ul0;
      ADD2LOG("    carrier = %d\n", if_carrier);
    }

    hw_addr = NULL;
    if((s = get_sysfs_attr_by_path(sf_cdev, "address"))) {
      hw_addr = canon_str(s, strlen(s));
      ADD2LOG("    hw_addr = %s\n", hw_addr);
    }

    sf_dev = new_str(hd_read_sysfs_link(sf_cdev, "device"));
    if(sf_dev) {
      ADD2LOG("    net device: path = %s\n", hd_sysfs_id(sf_dev));
    }

    sf_drv_name = NULL;
    sf_drv = hd_read_sysfs_link(sf_dev, "driver");
    if(sf_drv) {
      sf_drv_name = strrchr(sf_drv, '/');
      if(sf_drv_name) sf_drv_name++;
      ADD2LOG(
        "    net driver: name = %s, path = %s\n",
        sf_drv_name,
        hd_sysfs_id(sf_drv)
      );
    }

    hd = add_hd_entry(hd_data, __LINE__, 0);
    hd->base_class.id = bc_network_interface;
    hd->sub_class.id = sc_nif_other;

    res1 = NULL;
    if(hw_addr && strspn(hw_addr, "0:") != strlen(hw_addr)) {
      res1 = new_mem(sizeof *res1);
      res1->hwaddr.type = res_hwaddr;
      res1->hwaddr.addr = new_str(hw_addr);
      add_res_entry(&hd->res, res1);
    }

    res2 = NULL;
    if(if_carrier >= 0) {
      res = new_mem(sizeof *res);
      res->link.type = res_link;
      res->link.state = if_carrier ? 1 : 0;
      add_res_entry(&hd->res, res);
    }

    hd->unix_dev_name = new_str(sf_class_e->str);
    hd->sysfs_id = new_str(hd_sysfs_id(sf_cdev));

    if(sf_drv_name) {
      add_str_list(&hd->drivers, sf_drv_name);
    }
    else if(hd->res) {
      get_driverinfo(hd_data, hd);
    }

    switch(if_type) {
      case ARPHRD_ETHER:	/* eth */
        hd->sub_class.id = sc_nif_ethernet;
        break;
      case ARPHRD_LOOPBACK:	/* lo */
        hd->sub_class.id = sc_nif_loopback;
        break;
      case ARPHRD_SIT:		/* sit */
        hd->sub_class.id = sc_nif_sit;
        break;
      case ARPHRD_FDDI:		/* fddi */
        hd->sub_class.id = sc_nif_fddi;
        break;
      case ARPHRD_IEEE802_TR:	/* tr */
        hd->sub_class.id = sc_nif_tokenring;
        break;
#if 0
      case ARPHRD_IEEE802:	/* fc */
        hd->sub_class.id = sc_nif_fc;
        break;
#endif
      default:
        hd->sub_class.id = sc_nif_other;
    }

    if(!strcmp(hd->unix_dev_name, "lo")) {
      hd->sub_class.id = sc_nif_loopback;
    }
    else if(sscanf(hd->unix_dev_name, "eth%u", &u) == 1) {
      hd->sub_class.id = sc_nif_ethernet;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "tr%u", &u) == 1) {
      hd->sub_class.id = sc_nif_tokenring;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "fddi%u", &u) == 1) {
      hd->sub_class.id = sc_nif_fddi;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "ctc%u", &u) == 1) {
      hd->sub_class.id = sc_nif_ctc;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "iucv%u", &u) == 1) {
      hd->sub_class.id = sc_nif_iucv;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "hsi%u", &u) == 1) {
      hd->sub_class.id = sc_nif_hsi;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "qeth%u", &u) == 1) {
      hd->sub_class.id = sc_nif_qeth;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "escon%u", &u) == 1) {
      hd->sub_class.id = sc_nif_escon;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "myri%u", &u) == 1) {
      hd->sub_class.id = sc_nif_myrinet;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "sit%u", &u) == 1) {
      hd->sub_class.id = sc_nif_sit;	/* ipv6 over ipv4 tunnel */
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "wlan%u", &u) == 1) {
      hd->sub_class.id = sc_nif_wlan;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "xp%u", &u) == 1) {
      hd->sub_class.id = sc_nif_xp;
      hd->slot = u;
    }
    else if(sscanf(hd->unix_dev_name, "usb%u", &u) == 1) {
      hd->sub_class.id = sc_nif_usb;
      hd->slot = u;
    }
    /* ##### add more interface names here */
    else {
      for(s = hd->unix_dev_name; *s; s++) if(isdigit(*s)) break;
      if(*s && (u = strtoul(s, &s, 10), !*s)) {
        hd->slot = u;
      }
    }

    hd->bus.id = bus_none;

    hd_card = NULL;

    if(sf_dev) {
      s = new_str(hd_sysfs_id(sf_dev));

      hd->sysfs_device_link = new_str(s);

      hd_card = hd_find_sysfs_id(hd_data, s);

      // try one above, if not found
      if(!hd_card) {
        t = strrchr(s, '/');
        if(t) {
          *t = 0;
          hd_card = hd_find_sysfs_id(hd_data, s);
        }
      }

      /* if one card has several interfaces (as with PS3), check interface names, too */
      if(
        hd_card &&
        hd_card->unix_dev_name &&
        hd->unix_dev_name &&
        strcmp(hd->unix_dev_name, hd_card->unix_dev_name)
      ) {
        hd_card = hd_find_sysfs_id_devname(hd_data, s, hd->unix_dev_name);
      }

      s = free_mem(s);

      if(hd_card) {
        hd->attached_to = hd_card->idx;

        /* for cards with strange pci classes */
        hd_set_hw_class(hd_card, hw_network_ctrl);

        /* add hw addr to network card */
        if(res1) {
          u = 0;
          for(res = hd_card->res; res; res = res->next) {
            if(
              res->any.type == res_hwaddr &&
              !strcmp(res->hwaddr.addr, res1->hwaddr.addr)
            ) {
              u = 1;
              break;
            }
          }
          if(!u) {
            res = new_mem(sizeof *res);
            res->hwaddr.type = res_hwaddr;
            res->hwaddr.addr = new_str(res1->hwaddr.addr);
            add_res_entry(&hd_card->res, res);
          }
        }
        /*
         * add interface names...
         * but not wmasterX (bnc #441778)
         */
        if(if_type != 801) add_if_name(hd_card, hd);
      }
    }

    if(!hd_card && hw_addr) {
      /* try to find card based on hwaddr (for prom-based cards) */

      for(hd_card = hd_data->hd; hd_card; hd_card = hd_card->next) {
        if(
          hd_card->base_class.id != bc_network ||
          hd_card->sub_class.id != 0
        ) continue;
        for(res = hd_card->res; res; res = res->next) {
          if(
            res->any.type == res_hwaddr &&
            !strcmp(hw_addr, res->hwaddr.addr)
          ) break;
        }
        if(res) {
          hd->attached_to = hd_card->idx;
          break;
        }
      }
    }

    hw_addr = free_mem(hw_addr);

    /* fix card type */
    if(hd_card) {
      if(
        (hd_card->base_class.id == 0 && hd_card->sub_class.id == 0) ||
        (hd_card->base_class.id == bc_network && hd_card->sub_class.id == 0x80)
      ) {
        switch(hd->sub_class.id) {
          case sc_nif_ethernet:
            hd_card->base_class.id = bc_network;
            hd_card->sub_class.id = 0;
            break;

          case sc_nif_usb:
            hd_card->base_class.id = bc_network;
            hd_card->sub_class.id = 0x91;
            break;
        }
      }
    }

    sf_dev = free_mem(sf_dev);
  }

  sf_cdev = free_mem(sf_cdev);
  sf_class = free_str_list(sf_class);

  if(hd_is_sgi_altix(hd_data)) add_xpnet(hd_data);
  add_uml(hd_data);
  add_kma(hd_data);

  /* add link status info & dump eeprom */
  for(hd = hd_data->hd ; hd; hd = hd->next) {
    if(
      hd->module == hd_data->module &&
      hd->base_class.id == bc_network_interface
    ) {
      char *buf = NULL;
      str_list_t *sl0, *sl;

      if(hd_probe_feature(hd_data, pr_net_eeprom) && hd->unix_dev_name) {
        PROGRESS(2, 0, "eeprom dump");

        str_printf(&buf, 0, "|/usr/sbin/ethtool -e %s 2>/dev/null", hd->unix_dev_name);
        if((sl0 = read_file(buf, 0, 0))) {
          ADD2LOG("----- %s %s -----\n", hd->unix_dev_name, "EEPROM dump");
          for(sl = sl0; sl; sl = sl->next) {
            ADD2LOG("%s", sl->str);
          }
          ADD2LOG("----- %s end -----\n", "EEPROM dump");
          free_str_list(sl0);
        }
        free(buf);
      }

      for(res = hd->res; res; res = res->next) {
        if(res->any.type == res_link) break;
      }

      if(!res) get_linkstate(hd_data, hd);

      if(!(hd_card = hd_get_device_by_idx(hd_data, hd->attached_to))) continue;

      for(res = hd->res; res; res = res->next) {
        if(res->any.type == res_link) break;
      }

      if(res) {
        for(res1 = hd_card->res; res1; res1 = res1->next) {
          if(res1->any.type == res_link) break;
        }
        if(res && !res1) {
          res1 = new_mem(sizeof *res1);
          res1->link.type = res_link;
          res1->link.state = res->link.state;
          add_res_entry(&hd_card->res, res1);
        }
      }
    }
  }
}


/*
 * Get it the classical way, for drivers that don't support sysfs (veth).
 */
void get_driverinfo(hd_data_t *hd_data, hd_t *hd)
{
  int fd;
  struct ethtool_drvinfo drvinfo = { cmd:ETHTOOL_GDRVINFO };
  struct ifreq ifr;

  if(!hd->unix_dev_name) return;

  if(strlen(hd->unix_dev_name) > sizeof ifr.ifr_name - 1) return;

  if((fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) return;

  /* get driver info */
  memset(&ifr, 0, sizeof ifr);
  strcpy(ifr.ifr_name, hd->unix_dev_name);
  ifr.ifr_data = (caddr_t) &drvinfo;
  if(ioctl(fd, SIOCETHTOOL, &ifr) == 0) {
    ADD2LOG("    ethtool driver: %s\n", drvinfo.driver);
    ADD2LOG("    ethtool    bus: %s\n", drvinfo.bus_info);

    add_str_list(&hd->drivers, drvinfo.driver);
  }
  else {
    ADD2LOG("    GDRVINFO ethtool error: %s\n", strerror(errno));
  }

  close(fd);
}


/*
 * Check network link status.
 */
void get_linkstate(hd_data_t *hd_data, hd_t *hd)
{
  int fd;
  struct ethtool_value linkstatus = { cmd:ETHTOOL_GLINK };
  struct ifreq ifr;
  hd_res_t *res;

  if(!hd->unix_dev_name) return;

  if(strlen(hd->unix_dev_name) > sizeof ifr.ifr_name - 1) return;

  if((fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) return;

  /* get driver info */
  memset(&ifr, 0, sizeof ifr);
  strcpy(ifr.ifr_name, hd->unix_dev_name);
  ifr.ifr_data = (caddr_t) &linkstatus;
  if(ioctl(fd, SIOCETHTOOL, &ifr) == 0) {
    ADD2LOG("  %s: ethtool link state: %d\n", hd->unix_dev_name, linkstatus.data);
    res = new_mem(sizeof *res);
    res->link.type = res_link;
    res->link.state = linkstatus.data ? 1 : 0;
    add_res_entry(&hd->res, res);
  }
  else {
    ADD2LOG("  %s: GLINK ethtool error: %s\n", hd->unix_dev_name, strerror(errno));
  }

  close(fd);
}


/*
 * SGI Altix cross partition network.
 */
void add_xpnet(hd_data_t *hd_data)
{
  hd_t *hd, *hd_card;
  hd_res_t *res, *res2;

  hd_card = add_hd_entry(hd_data, __LINE__, 0);
  hd_card->base_class.id = bc_network;
  hd_card->sub_class.id = 0x83;

  hd_card->vendor.id = MAKE_ID(TAG_SPECIAL, 0x4002);
  hd_card->device.id = MAKE_ID(TAG_SPECIAL, 1);

  if(hd_module_is_active(hd_data, "xpnet")) {
    add_str_list(&hd_card->drivers, "xpnet");
  }

  for(hd = hd_data->hd ; hd; hd = hd->next) {
    if(
      hd->module == hd_data->module &&
      hd->base_class.id == bc_network_interface &&
      hd->sub_class.id == sc_nif_xp
    ) {
      hd->attached_to = hd_card->idx;

      for(res = hd->res; res; res = res->next) {
        if(res->any.type == res_hwaddr) break;
      }

      if(res) {
        res2 = new_mem(sizeof *res2);
        res2->hwaddr.type = res_hwaddr;
        res2->hwaddr.addr = new_str(res->hwaddr.addr);
        add_res_entry(&hd_card->res, res2);
      }

      add_if_name(hd_card, hd);

      break;
    }
  }
}






/*
 * UML veth devices.
 */
void add_uml(hd_data_t *hd_data)
{
  hd_t *hd, *hd_card;
  hd_res_t *res, *res2;
  unsigned card_cnt = 0;

  for(hd = hd_data->hd ; hd; hd = hd->next) {
    if(
      hd->module == hd_data->module &&
      hd->base_class.id == bc_network_interface &&
      search_str_list(hd->drivers, "uml virtual ethernet")
    ) {
      hd_card = add_hd_entry(hd_data, __LINE__, 0);
      hd_card->base_class.id = bc_network;
      hd_card->sub_class.id = 0x00;
      hd_card->vendor.id = MAKE_ID(TAG_SPECIAL, 0x6010);	// UML
      hd_card->device.id = MAKE_ID(TAG_SPECIAL, 0x0001);
      hd_card->slot = card_cnt++;
      str_printf(&hd_card->device.name, 0, "Virtual Ethernet card %d", hd_card->slot);

      hd->attached_to = hd_card->idx;

      for(res = hd->res; res; res = res->next) {
        if(res->any.type == res_hwaddr) break;
      }

      if(res) {
        res2 = new_mem(sizeof *res2);
        res2->hwaddr.type = res_hwaddr;
        res2->hwaddr.addr = new_str(res->hwaddr.addr);
        add_res_entry(&hd_card->res, res2);
      }

      add_if_name(hd_card, hd);
    }
  }
}


/*
 * KMA veth devices.
 */
void add_kma(hd_data_t *hd_data)
{
  hd_t *hd, *hd_card;
  hd_res_t *res, *res2;
  unsigned card_cnt = 0;

  for(hd = hd_data->hd ; hd; hd = hd->next) {
    if(
      hd->module == hd_data->module &&
      hd->base_class.id == bc_network_interface &&
      search_str_list(hd->drivers, "kveth2")
    ) {
      hd_card = add_hd_entry(hd_data, __LINE__, 0);
      hd_card->base_class.id = bc_network;
      hd_card->sub_class.id = 0x00;
      hd_card->vendor.id = MAKE_ID(TAG_SPECIAL, 0x6012);	// VirtualIron
      hd_card->device.id = MAKE_ID(TAG_SPECIAL, 0x0001);
      hd_card->slot = card_cnt++;
      str_printf(&hd_card->device.name, 0, "Ethernet card %d", hd_card->slot);

      hd->attached_to = hd_card->idx;

      for(res = hd->res; res; res = res->next) {
        if(res->any.type == res_hwaddr) break;
      }

      if(res) {
        res2 = new_mem(sizeof *res2);
        res2->hwaddr.type = res_hwaddr;
        res2->hwaddr.addr = new_str(res->hwaddr.addr);
        add_res_entry(&hd_card->res, res2);
      }

      add_if_name(hd_card, hd);
    }
  }
}


/*
 * add interface name to card
 */
void add_if_name(hd_t *hd_card, hd_t *hd)
{
  str_list_t *sl0;

  if(hd->unix_dev_name) {
    if(!search_str_list(hd_card->unix_dev_names, hd->unix_dev_name)) {
      if(hd->sub_class.id == sc_nif_other) {
        /* add at end */
        add_str_list(&hd_card->unix_dev_names, hd->unix_dev_name);
      }
      else {
        /* add at top */
        sl0 = new_mem(sizeof *sl0);
        sl0->next = hd_card->unix_dev_names;
        sl0->str = new_str(hd->unix_dev_name);
        hd_card->unix_dev_names = sl0;
      }
      free_mem(hd_card->unix_dev_name);
      hd_card->unix_dev_name = new_str(hd_card->unix_dev_names->str);
    }
  }
}

/** @} */

