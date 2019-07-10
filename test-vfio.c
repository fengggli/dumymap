#include <fcntl.h>
#include <linux/vfio.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <assert.h>
#include <errno.h>
#include <string.h>

#define PINF(f_, ...) printf((f_ "\n"), ##__VA_ARGS__)
#define PERR(f_, ...) fprintf(stderr, (f_ "\n"), ##__VA_ARGS__)

#define VFIO_GROUP_PATH ("/dev/vfio/14")
#define DEVICE_PCI ("0000:11:00.0")

typedef enum {
  MEM_TYPE_ANON_REGULAR = 0, // anonymous mmap
  MEM_TYPE_DUMMYMAP = 1,     // filled by dummymap module (remap_pfn_range)
  MEM_TYPE_DEVDAX = 2,
} mem_type_t;

/*static const mem_type_t k_mem_type = MEM_TYPE_DEVDAX;*/
/*static const mem_type_t mem_type = MEM_TYPE_ANON_REGULAR;*/
static const mem_type_t k_mem_type = MEM_TYPE_DUMMYMAP;

int run_register_dma(mem_type_t mem_type) {
  int container, group, device, i;
  struct vfio_group_status group_status = {.argsz = sizeof(group_status)};
  struct vfio_iommu_type1_info iommu_info = {.argsz = sizeof(iommu_info)};
  struct vfio_iommu_type1_dma_map dma_map = {.argsz = sizeof(dma_map)};
  struct vfio_device_info device_info = {.argsz = sizeof(device_info)};

  /* Create a new container */
  container = open("/dev/vfio/vfio", O_RDWR);

  if (ioctl(container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
    /* Unknown API version */
    PERR("VFIO API unmatch");
    return -1;
  }

  if (!ioctl(container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
    /* Doesn't support the IOMMU driver we want. */
    PERR("VFIO extension unmatch");
    return -1;
  }

  /* Open the group */
  group = open(VFIO_GROUP_PATH, O_RDWR);
  if (group < 0) {
    PERR("open group path failed at %s (change VFIO_GROUP_PATH)",
         VFIO_GROUP_PATH);
    return -1;
  }

  /* Test the group is viable and available */
  if (0 > ioctl(group, VFIO_GROUP_GET_STATUS, &group_status)) {
    PERR("ioctl failed with VFIO_GROUP_GET_STATUS");
    return -1;
  }

  if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
    /* Group is not viable (ie, not all devices bound for vfio) */
    PERR("Group is not viable (ie, not all devices bound for vfio)");
    return -1;
  }

  /* Add the group to the container */
  if (0 > ioctl(group, VFIO_GROUP_SET_CONTAINER, &container)) {
    PERR("Add the group to the container");
    return -1;
  };

  /* Enable the IOMMU model we want */
  ioctl(container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);

  /* Get addition IOMMU info */
  ioctl(container, VFIO_IOMMU_GET_INFO, &iommu_info);
  size_t map_size = 2 * 1024 * 1024; // 2MB
  void *target_addr = ((void *)0x900000000);
  int fd;
  void *ptr;

  /* Allocate some space and setup a DMA mapping */
  switch (mem_type) {
  case MEM_TYPE_DEVDAX:
    // https://github.com/axboe/fio/blob/master/engines/dev-dax.c
    fd = open("/dev/dax0.0", O_RDWR, 0666);
    assert(fd != -1);
    ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
               // MAP_PRIVATE | MAP_FIXED, // | MAP_HUGETLB, // | MAP_HUGE_2MB,
               MAP_SHARED, fd, 0);
    assert(ptr != MAP_FAILED);
    // memset(ptr, 0xff, map_size);

    break;

  case MEM_TYPE_DUMMYMAP:
    fd = open("/dev/dummymap", O_RDWR, 0666);
    assert(fd != -1);
    ptr = mmap(target_addr, map_size, PROT_READ | PROT_WRITE,
               // MAP_PRIVATE | MAP_FIXED, // | MAP_HUGETLB, // | MAP_HUGE_2MB,
               MAP_SHARED | MAP_FIXED, fd, 0);
    assert(ptr != MAP_FAILED);
    // memset(ptr, 0xff, map_size);

    break;
  case MEM_TYPE_ANON_REGULAR:
  default:
    dma_map.vaddr = mmap(target_addr, map_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0, 0);
  }

  dma_map.size = map_size;
  dma_map.iova = 0x900000000; /* 1MB starting at 0x900000000 from device view */
  dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

  PINF("press enter to map issue VFIO_IOMMU_MAP_DMA");
  getchar();
  if (0 > ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map)) {
    PERR("VFIO_IO_MMU_MAP_DMA failed with errno %s", strerror(errno));
    return -1;
  } else {
    PINF("DMA memory setup succeed!");
  }

  /* Get a file descriptor for the device */
  device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, DEVICE_PCI);
  if (device < 0) {
    PERR("IOCTRL with errno during get_device_fd (Change DEVICE_PCI): %s",
         strerror(errno));
    return -1;
  }

  /* Test and setup the device */
  if (0 > ioctl(device, VFIO_DEVICE_GET_INFO, &device_info)) {
    PERR("IOCTRL with errno during get_info: %s", strerror(errno));
    return -1;
  }

  for (i = 0; i < device_info.num_regions; i++) {
    struct vfio_region_info reg = {.argsz = sizeof(reg)};

    reg.index = i;

    ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &reg);

    /* Setup mappings... read/write offsets, mmaps
     * For PCI devices, config space is a region */
  }

  for (i = 0; i < device_info.num_irqs; i++) {
    struct vfio_irq_info irq = {.argsz = sizeof(irq)};

    irq.index = i;

    ioctl(device, VFIO_DEVICE_GET_IRQ_INFO, &irq);

    /* Setup IRQs... eventfds, VFIO_DEVICE_SET_IRQS */
  }

  /* Gratuitous device reset and go... */
  ioctl(device, VFIO_DEVICE_RESET);
}

int main(int argc, char *argv[]) {

  int mem_type = MEM_TYPE_ANON_REGULAR;
  if (argc > 1) {
    mem_type = atoi(argv[1]);
    if (mem_type > 2) {
      PERR("invalid memory type (1: dummymap, 2: devdax map)");
      return -1;
    }
  }

  PINF("Using memory type %d(0: anonymous mmap, 1: dummymap, 2: devdax map)",
       mem_type);
  return run_register_dma(mem_type);
}
