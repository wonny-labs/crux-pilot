/* Host stand-in for the devicetree macros that the bench medium glue reads: the geometry of the RAM disk
 * node. The values are the ones of ../../../ram_disk.overlay, which a unit test compares with this. */
#ifndef HOST_SHIM_ZEPHYR_DEVICETREE_H
#define HOST_SHIM_ZEPHYR_DEVICETREE_H
#define DT_NODELABEL(label) label
#define DT_PROP(node, prop) DT_PROP_(node, prop)
#define DT_PROP_(node, prop) node##_##prop
#ifndef bench_ram_disk_sector_size
#define bench_ram_disk_sector_size 512
#define bench_ram_disk_sector_count 1024
#endif
#endif
