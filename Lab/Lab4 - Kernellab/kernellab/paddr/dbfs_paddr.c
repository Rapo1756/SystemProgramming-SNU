/*
 * Development Environment :
 * Ubuntu 20.04.6 LTS (Focal Fossa)
 * Linux kjj-Ubuntu 5.15.0-72-generic #79~20.04.1-Ubuntu SMP
 * Thu Apr 20 22:12:07 UTC 2023 x86_64 x86_64 x86_64 GNU/Linux
 */

#include <asm/pgtable.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pgtable.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");

static struct dentry *dir, *output;
static struct task_struct *task;

// struct for input
struct packet {
  pid_t pid;
  unsigned long vaddr;
  unsigned long paddr;
};

static ssize_t read_output(struct file *fp, char __user *user_buffer,
                           size_t length, loff_t *position) {
  // declare necessary variables
  struct packet pckt;
  struct mm_struct *mm;
  pgd_t *pgd;
  p4d_t *p4d;
  pud_t *pud;
  pmd_t *pmd;
  pte_t *ptep, pte;
  unsigned long pfn, vpo;

  struct page *page = NULL;
  // get pid and vaddr from user_buffer using struct packet
  if (copy_from_user(&pckt, user_buffer, length))
    return -EFAULT;

  // find task_struct using pid
  task = pid_task(find_get_pid(pckt.pid), PIDTYPE_PID);
  if (!task)
    return -EINVAL;

  mm = task->mm;
  if (!mm)
    return -EINVAL;

  // page table walk using mm and vaddr
  pgd = pgd_offset(mm, pckt.vaddr);
  if (pgd_none(*pgd) || pgd_bad(*pgd))
    return -EINVAL;

  p4d = p4d_offset(pgd, pckt.vaddr);
  if (p4d_none(*p4d) || p4d_bad(*p4d))
    return -EINVAL;

  pud = pud_offset(p4d, pckt.vaddr);
  if (pud_none(*pud) || pud_bad(*pud))
    return -EINVAL;

  pmd = pmd_offset(pud, pckt.vaddr);
  if (pmd_none(*pmd) || pmd_bad(*pmd))
    return -EINVAL;

  ptep = pte_offset_map(pmd, pckt.vaddr);
  if (!ptep)
    return -EINVAL;

  // get pfn from pte
  pte = *ptep;
  pte_unmap(ptep);
  if (!pte_present(pte))
    return -EINVAL;

  page = pte_page(pte);
  if (!page)
    return -EINVAL;

  pfn = page_to_pfn(page);

  // fill in pckt.paddr using pfn and vaddr
  vpo = pckt.vaddr & ~PAGE_MASK;
  pckt.paddr = (pfn << PAGE_SHIFT) | vpo;

  // copy pckt to user_buffer
  if (copy_to_user(user_buffer, &pckt, length))
    return -EFAULT;
  return length;
}

static const struct file_operations dbfs_fops = {
    .read = read_output,
};

static int __init dbfs_module_init(void) {
  // create paddr module directory
  dir = debugfs_create_dir("paddr", NULL);
  if (!dir) {
    printk("Cannot create paddr dir\n");
    return -1;
  }

  // create output file under module directory
  output = debugfs_create_file("output", S_IRWXUGO, dir, NULL, &dbfs_fops);
  if (!output) {
    printk("Cannot create output file\n");
    return -1;
  }

  printk("dbfs_paddr module initialize done\n");
  return 0;
}

static void __exit dbfs_module_exit(void) {
  // remove paddr module directory using debugfs_remove_recursive
  debugfs_remove_recursive(dir);
  printk("dbfs_paddr module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
