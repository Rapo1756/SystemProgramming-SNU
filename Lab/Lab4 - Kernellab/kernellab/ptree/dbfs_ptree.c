/*
 * Development Environment :
 * Ubuntu 20.04.6 LTS (Focal Fossa)
 * Linux kjj-Ubuntu 5.15.0-72-generic #79~20.04.1-Ubuntu SMP
 * Thu Apr 20 22:12:07 UTC 2023 x86_64 x86_64 x86_64 GNU/Linux
 */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define BUFFER_SIZE 100000

MODULE_LICENSE("GPL");

static struct dentry *dir, *inputdir, *ptreedir;
static struct task_struct *curr;
static struct debugfs_blob_wrapper blob;

typedef struct {
  struct task_struct *task;
  struct list_head list;
} task_node;

// write pid to input file
static ssize_t write_pid_to_input(struct file *fp,
                                  const char __user *user_buffer, size_t length,
                                  loff_t *position) {
  pid_t input_pid;
  // get pid from user_buffer
  if (copy_from_user(blob.data, user_buffer, length))
    return -EFAULT;
  sscanf(blob.data, "%u", &input_pid);

  // find task_struct using pid
  curr = pid_task(find_get_pid(input_pid), PIDTYPE_PID);
  if (!curr)
    return -EINVAL;

  // print all ancestor processes using list
  LIST_HEAD(task_list);
  while(curr->pid != 1){
    task_node *curr_node = kmalloc(sizeof(task_node), GFP_KERNEL);
    curr_node->task = curr;
    list_add(&curr_node->list, &task_list);
    curr = curr->real_parent;
  }
  blob.size = snprintf(blob.data, BUFFER_SIZE,
                        "%s (%d)\n", curr->comm, curr->pid);
  task_node *p;
  list_for_each_entry(p, &task_list, list){
    blob.size += snprintf(blob.data + blob.size, BUFFER_SIZE - blob.size,
                        "%s (%d)\n", p->task->comm, p->task->pid);
  }
  return length;
}

static const struct file_operations dbfs_fops = {
    .write = write_pid_to_input,
};

static int __init dbfs_module_init(void) {
  // initialize blob
  static char buffer[BUFFER_SIZE];
  blob.data = buffer;

  // create ptree module directory
  dir = debugfs_create_dir("ptree", NULL);
  if (!dir) {
    printk("Cannot create ptree dir\n");
    return -1;
  }

  // create input file under ptree directory
  inputdir =
      debugfs_create_file("input", S_IRWXUGO, dir, blob.data, &dbfs_fops);
  if (!inputdir) {
    printk("Cannot create input dir\n");
    return -1;
  }
  // create output file under ptree directory
  ptreedir = debugfs_create_blob("ptree", S_IRWXUGO, dir, &blob);
  if (!ptreedir) {
    printk("Cannot create ptree dir\n");
    return -1;
  }

  printk("dbfs_ptree module initialize done\n");
  return 0;
}

static void __exit dbfs_module_exit(void) {
  // Remove ptree module directory
  debugfs_remove_recursive(dir);
  printk("dbfs_ptree module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);