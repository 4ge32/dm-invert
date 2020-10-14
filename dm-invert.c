#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/device-mapper.h>
#include <linux/slab.h>

#define DM_MSG_PREFIX "dm-invert"

/* For underlying device */
struct invert_device {
	struct dm_dev *dev;
	sector_t start;
	unsigned int blksz;
	bool readable;
};

static void do_invert(struct bio_vec *bvec, struct bvec_iter *i)
{
	char *buf;
	sector_t sector = (*i).bi_sector;
	unsigned long offset = (*bvec).bv_offset;
	size_t len = (*bvec).bv_len;
	size_t cnt = 0;

	DMINFO("sector(%lld)"
	       "offset(%ld)"
	       "len(%ld)\n", sector, offset, len);

	buf = kmap_atomic((*bvec).bv_page);
	for (cnt = 0; cnt < len; cnt++) {
		char *b = buf + cnt;
		*b = ~(*b);
	}
	kunmap_atomic(buf);
}

/* Map function, called whenever target gets a bio request. */
static int dm_invert_map(struct dm_target *target, struct bio *bio)
{
	struct invert_device *ide = (struct invert_device *)target->private;
	struct bio_vec bvec;
	struct bvec_iter i;

	DMINFO("Entry: %s", __func__);

	/*  bio should perform on our underlying device   */
	bio_set_dev(bio, ide->dev->bdev);

	if (bio_data_dir(bio) == WRITE) {
		DMINFO("bio is a write request");
		bio_for_each_segment(bvec, bio, i) {
			do_invert(&bvec, &i);
		}
	} else {
		DMINFO("bio is a read request");
		bio_for_each_segment(bvec, bio, i) {
			sector_t sector = i.bi_sector;
			unsigned long offset = bvec.bv_offset;
			size_t len = bvec.bv_len;
			DMINFO("sector(%lld)"
			        "offset(%ld)"
				"len(%ld)\n", sector, offset, len);
		}
	}

	DMINFO("Exit : %s", __func__);
	return DM_MAPIO_REMAPPED;
}

/*
 * Target parameters:
 *
 * <device_path> <offset> <blksz>
 *
 * device_path: path to the block device
 * offset: offset to data area from start of device_path
 * blksz: block size (minimum 512, maximum 1073741824, must be a power of 2)
 */
static int dm_invert_ctr(struct dm_target *target,
			 unsigned int argc, char **argv)
{
	struct invert_device *ide;
	int ret = 0;
	unsigned int blksz = 0;
	sector_t INVERT_MAX_BLKSZ_SECTORS = 2097152;
	sector_t max_block_sectors = min(target->len, INVERT_MAX_BLKSZ_SECTORS);
	unsigned long long tmp;
	char dummy;

	DMINFO("Entry: %s", __func__);

	if (argc != 3) {
		char *emsg = "Invalid argument count";
		DMERR("%s\n", emsg);
		target->error = emsg;
		return -EINVAL;
	}

	if (kstrtouint(argv[2], 10, &blksz) || !blksz) {
		char *emsg = "Invalid block size parameter";
		DMERR("%s\n", emsg);
		target->error = emsg;
		return -EINVAL;
	}

	if (blksz < 512) {
		char *emsg = "Invalid block size paramter";
		DMERR("%s\n", emsg);
		target->error = emsg;
		return -EINVAL;
	}

	if (!is_power_of_2(blksz)) {
		char *emsg = "Block size must be a power of 2";
		DMERR("%s\n", emsg);
		target->error = emsg;
		return -EINVAL;
	}

	if (to_sector(blksz) > max_block_sectors) {
		char *emsg = "Block size is too large";
		DMERR("%s\n", emsg);
		target->error = emsg;
		return -EINVAL;
	}

	if (sscanf(argv[1], "%llu%c", &tmp, &dummy) != 1
	    || tmp != (sector_t)tmp) {
		char *emsg = "Invalid device offset sector";
		DMERR("%s\n", emsg);
		target->error = emsg;
		return -EINVAL;
	}

	ide = kzalloc(sizeof(struct invert_device), GFP_KERNEL);
	if (!ide) {
		char *emsg = "Cannot allocate context";
		DMERR("%s\n", emsg);
		target->error = emsg;
		ret = -ENOMEM;
	}

	if (dm_get_device(target, argv[0],
			  dm_table_get_mode(target->table), &ide->dev)) {
		char *emsg = "Cannot allocate context";
		DMERR("%s\n", emsg);
		target->error = emsg;
		kfree(ide);
		ret = -EINVAL;
	}
	ide->start= tmp;
	ide->blksz = blksz;
	ide->readable = false;
	target->private = ide;

	DMINFO("Exit : %s ", __func__);
	return ret;
}

/*
 *  This is destruction function, gets called per device.
 *  It removes device and decrement device count.
 */
static void dm_invert_dtr(struct dm_target *target)
{
	struct invert_device *ide = (struct invert_device *)target->private;
	DMINFO("Entry: %s", __func__);
	dm_put_device(target, ide->dev);
	kfree(ide);
	DMINFO("Exit : %s", __func__);
}

static void dm_status(struct dm_target *target, status_type_t type,
		      unsigned int status_flags, char *result, unsigned int maxlen)
{
	struct invert_device *ide = target->private;
	unsigned int sz = 0;

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%s %s", ide->dev->name,
		       ide->readable ? "read correctly" : "read raw data");
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%s %llu %u", ide->dev->name,
		       (unsigned long long)ide->start, ide->blksz);
		break;
	}
}

static struct target_type invert_target = {

	.name    = "invert",
	.version = {0,0,1},
	.module  = THIS_MODULE,
	.ctr     = dm_invert_ctr,
	.dtr     = dm_invert_dtr,
	.status  = dm_status,
	.map     = dm_invert_map,
};

/*---------Module Functions -----------------*/

static int init_dm_invert(void)
{
	int res;
	DMINFO("Entry: %s", __func__);
	res = dm_register_target(&invert_target);
	if (res< 0) {
		DMERR("Error in registering target");
	} else {
		DMINFO("Target registered");
	}
	DMINFO("Exit : %s", __func__);
	return 0;
}


static void cleanup_dm_invert(void)
{
	DMINFO("Entry: %s", __func__);
	dm_unregister_target(&invert_target);
	DMINFO("Exit : %s", __func__);
}

module_init(init_dm_invert);
module_exit(cleanup_dm_invert);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fumiya Shigemitsu");
