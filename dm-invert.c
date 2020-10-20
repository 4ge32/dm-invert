#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/device-mapper.h>
#include <linux/slab.h>

#define DM_MSG_PREFIX "dm-invert"

struct invert_block {
	struct rb_node node;
	sector_t bb;
};

/* For underlying device */
struct invert_device {
	struct dm_dev *dev;
	struct rb_root invert_blocklist;
	sector_t start;
	unsigned int blksz;
	spinlock_t invert_lock;
	bool readable;
};

static void one_fill_bio(struct bio *bio)
{
	struct bio_vec bvec;
	struct bvec_iter i;
	unsigned long flags;

	DMINFO("Entry: %s", __func__);

	bio_for_each_segment(bvec, bio, i) {
		char *buf;
		buf = bvec_kmap_irq(&bvec, &flags);
		memset(buf, 0xff, bvec.bv_len);
		flush_dcache_page(bvec.bv_page);
		bvec_kunmap_irq(buf, &flags);
	}
}

/* Map function, called whenever target gets a bio request. */
static int invert_map(struct dm_target *target, struct bio *bio)
{
	struct invert_device *ide = (struct invert_device *)target->private;

	DMINFO("Entry: %s", __func__);

	switch (bio_op(bio)) {
	case REQ_OP_READ:
		if (ide->readable)
			zero_fill_bio(bio);
		else
			one_fill_bio(bio);
		break;
	case REQ_OP_WRITE:
		one_fill_bio(bio);
		break;
	default:
		return DM_MAPIO_KILL;
	}

	bio_endio(bio);

	DMINFO("Exit: %s", __func__);

	return DM_MAPIO_SUBMITTED;
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
static int invert_ctr(struct dm_target *target,	unsigned int argc, char **argv)
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
	ide->invert_blocklist = RB_ROOT;
	ide->start= tmp;
	ide->blksz = blksz;
	ide->readable = false;
	spin_lock_init(&ide->invert_lock);
	target->private = ide;

	DMINFO("Exit : %s ", __func__);
	return ret;
}

/*
 *  This is destruction function, gets called per device.
 *  It removes device and decrement device count.
 */
static void invert_dtr(struct dm_target *target)
{
	struct invert_device *ide = (struct invert_device *)target->private;
	DMINFO("Entry: %s", __func__);
	dm_put_device(target, ide->dev);
	kfree(ide);
	DMINFO("Exit : %s", __func__);
}

static void invert_status(struct dm_target *target, status_type_t type,
			  unsigned int status_flags, char *result,
			  unsigned int maxlen)
{
	struct invert_device *ide = target->private;
	unsigned int sz = 0;

	DMINFO("Entry: %s", __func__);
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
	DMINFO("Exit: %s", __func__);
}

static void switch_readable(struct invert_device *ide, bool readable)
{
	if (readable) {
		DMINFO("enabling correct reading");
		ide->readable = true;
	} else {
		DMINFO("disabling correct reading");
		ide->readable = false;
	}
}

static bool invert_rb_insert(struct rb_root *root, struct invert_block *new)
{
	struct invert_block *iblk;
	struct rb_node **link = &root->rb_node;
	struct rb_node *parent = NULL;
	sector_t val = new->bb;

	while (*link) {
		parent = *link;
		iblk = rb_entry(parent, struct invert_block, node);
		if (iblk->bb > val)
			link = &(*link)->rb_left;
		else if (iblk->bb < val)
			link = &(*link)->rb_right;
		else
			return false;
	}
	rb_link_node(&new->node, parent, link);
	rb_insert_color(&new->node, root);

	return true;
}

static int add_invert_block(struct invert_device *ide, unsigned long long block)
{
	struct invert_block *iblock;
	unsigned long flags = 0;
	int res;

	iblock = kmalloc(sizeof(struct invert_device), GFP_KERNEL);
	if (!iblock)
		return -ENOMEM;

	spin_lock_irqsave(&ide->invert_lock, flags);

	iblock->bb = block;
	res = invert_rb_insert(&ide->invert_blocklist, iblock);
	if (!res) {
		kfree(iblock);
		spin_unlock_irqrestore(&ide->invert_lock, flags);
		return -EINVAL;
	}

	spin_unlock_irqrestore(&ide->invert_lock, flags);

	return 0;
}

static int remove_invert_block(struct invert_device *ide, unsigned long long block)
{
	return -EINVAL;
}

static int invert_message(struct dm_target *target, unsigned int argc,
			  char **argv, char *result, unsigned int maxlen)
{
	struct invert_device *ide = target->private;
	bool invalid_msg = false;
	unsigned long long block;
	char dummy;
	int res = 0;

	DMINFO("Entry: %s", __func__);

	if (argc != 1) {
		return -EINVAL;
	}

	switch (argc) {
	case 1:
		if (!strcasecmp(argv[0], "enable"))
			switch_readable(ide, true);
		else if (!strcasecmp(argv[0], "disable"))
			switch_readable(ide, false);
		else
			invalid_msg = true;
		break;
	case 2:
		sscanf(argv[1], "%llu%c", &block, &dummy);
		if (!strcasecmp(argv[0], "addinvertblock"))
			res = add_invert_block(ide, block);
		else if (!strcasecmp(argv[0], "removeinvertblock"))
			res = remove_invert_block(ide, block);
		else
			invalid_msg = true;

		break;
	default:
		break;
	}

	DMINFO("Exit: %s", __func__);

	return res;
}

static struct target_type invert_target = {

	.name    = "invert",
	.version = {0,0,1},
	.module  = THIS_MODULE,
	.ctr     = invert_ctr,
	.dtr     = invert_dtr,
	.status  = invert_status,
	.message = invert_message,
	.map     = invert_map,
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
