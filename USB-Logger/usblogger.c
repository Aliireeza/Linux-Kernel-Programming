//We need this header in almost all Kernel modules
#include <linux/module.h>
//Absolutely because we are doing Kernel job
#include <linux/kernel.h>
//And this is needed for the macros
#include <linux/init.h>
//For create and register a procfs entry
#include <linux/proc_fs.h>
//For providing read function of the entry with ease
#include <linux/seq_file.h>
//For ioctl commands and macros
#include <linux/ioctl.h>
#include <asm/ioctl.h>
//For evaluating capabilities
#include <linux/capability.h>
//For using spinlocks
#include <linux/spinlock.h>
//For blocking I/O and waitqueues
#include <linux/wait.h>
//We want to play with USB devices
#include <linux/usb.h>
//For creating a queue by fifo structures
#include <linux/kfifo.h>
//For lookaside cache structures
#include <linux/slab.h>
//For using mempry pools
#include <linux/mempool.h>
//For "do_gettimeofday" or "current_kernel_time" or "getnstimeofday" functions
#include <linux/time.h>
//For obtaining PID and process name which demand some work from this module
#include <linux/sched.h>
//For raw_copy_to_user, raw_copy_from_user, put_user
#include <asm/uaccess.h>

#include "commonioctlcommands.h"

//It is always good to have a meaningful constant as a return code
#define SUCCESS 0
//This will be our module name
#define MODULE_NAME "usblogger"
//This is the constant that used for determination of buffer length
#define MAX_BUF_LEN 16
#define LOG_BUF_LEN 24

//These are some useful information that could reveald with modinfo command
//Set module license to get rid of tainted kernel warnings
MODULE_LICENSE("GPL");
//Introduce the module's developer, it's functionality and version
MODULE_AUTHOR("Aliireeza Teymoorian <teymoorian@gmail.com>");
MODULE_DESCRIPTION("USB Logger, Record the last 1024 USB ports activities on the system from the last boot");
MODULE_VERSION("1.0.0");



//Here are some useful variables
static atomic_t module_usage_flag = ATOMIC_INIT(1);
static struct kfifo dev_queue;
static struct kmem_cache *our_cache;
static mempool_t *log_pool;
static spinlock_t queue_usage_spinlock, linkedlist_usage_spinlock;;

//Creating a proc directory entry structure
static struct proc_dir_entry* log_proc_file;
static struct proc_dir_entry* dev_proc_file;

//Creating a waitequeue for yhe user process
static wait_queue_head_t our_queue;

//Queue and linkedlist variables
static int linkedlist_size = 0, linkedlist_count = 0;
typedef struct {char buf[MAX_BUF_LEN];} dev_queue_type;
static int queue_buffer_size;
static struct linkedlist_item{
	char buffer[LOG_BUF_LEN];
	struct linkedlist_item *next;
	struct linkedlist_item *perv;
	};
static struct linkedlist_item *list_head, *list_tail, *list_comm, *list_element, *list_list;

//Now we have to create a buffer for our longest message (MAX_BUF_LEN)
static char queue_buffer[MAX_BUF_LEN];
static char linkedlist_buffer[LOG_BUF_LEN];


//When device recive ioctl commands this function will perform the job depending on what kind of command it recieved
long log_proc_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	static int err = 0;
	char output[10];
	
	if(_IOC_TYPE(cmd) != LOG_MAGIC || _IOC_NR(cmd) > LOG_IOC_MAXNR)
		return -ENOTTY;

	if(_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_READ, (void __user *) arg, _IOC_SIZE(cmd));
	if(_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_WRITE, (void __user *) arg, _IOC_SIZE(cmd));
	if(err)
		return -EFAULT;
	
	switch(cmd){
		case IOCTL_LOG_RESET:
			//This command only works for system administrators
			if(!capable(CAP_SYS_ADMIN))
				return -EPERM;
			//Here just flush all datas and reset the pointer so you could use an empty queue
			//linkedlist_reset();
			break;
		case IOCTL_LOG_COUNT:
			//This is how we could obtain the number of logs in the queue
			sprintf(output, "%d", linkedlist_count);
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_LOG_SPACE:
			//This is how we could obtain how many empty room left in the queue for new logs
			sprintf(output, "%d", linkedlist_size - linkedlist_count); 
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_LOG_SIZE:
			//This ioctl signal will return the size of the queue in how many logs it could get
			sprintf(output, "%d", linkedlist_size); 
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_LOG_FULL:
			//Here we check whether the queue is full or not
			sprintf(output, "%d", linkedlist_size == linkedlist_count ? 1 : 0);
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_LOG_EMPTY:
			//Just like the previous condition but here we check whether it is empty or not
			sprintf(output, "%d", linkedlist_count == 0 ? 1 : 0);
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_LOG_ESIZE:
			//Return the size of the element of the list
			sprintf(output, "%d", MAX_BUF_LEN * 4);
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_LOG_DELETE:
			//Delete the specified log from the linkedlist
			//linkedlist_delete_item(arg);
			break;
		default:
			return -ENOTTY;
	}
	return SUCCESS;
}



long dev_proc_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	static int err = 0;
	char output[10];
	
	if(_IOC_TYPE(cmd) != DEV_MAGIC || _IOC_NR(cmd) > DEV_IOC_MAXNR)
		return -ENOTTY;

	if(_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_READ, (void __user *) arg, _IOC_SIZE(cmd));
	if(_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_WRITE, (void __user *) arg, _IOC_SIZE(cmd));
	if(err)
		return -EFAULT;
	
	switch(cmd){
		case IOCTL_DEV_RESET:
			//This command only works for system administrators
			if(!capable(CAP_SYS_ADMIN))
				return -EPERM;
			//Here just flush all datas and reset the pointer so you could use an empty queue
			kfifo_reset(&dev_queue);
			break;
		case IOCTL_DEV_COUNT:
			//This is how we could obtain the number of logs in the queue
			sprintf(output, "%ld", (kfifo_size(&dev_queue) - kfifo_avail(&dev_queue)) / sizeof(dev_queue_type));
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_DEV_SPACE:
			//This is how we could obtain how many empty room left in the queue for new logs
			sprintf(output, "%ld", kfifo_avail(&dev_queue) / sizeof(dev_queue_type)); 
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_DEV_SIZE:
			//This ioctl signal will return the size of the queue in how many logs it could get
			sprintf(output, "%ld", kfifo_size(&dev_queue) / sizeof(dev_queue_type)); 
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_DEV_FULL:
			//Here we check whether the queue is full or not
			sprintf(output, "%d", kfifo_is_full(&dev_queue));
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_DEV_EMPTY:
			//Just like the previous condition but here we check whether it is empty or not
			sprintf(output, "%d", kfifo_is_empty(&dev_queue));
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		case IOCTL_DEV_ESIZE:
			//Return the size of the element of the list
			sprintf(output, "%d", kfifo_esize(&dev_queue));
			if(raw_copy_to_user((int __user *) arg, output, 10)){
				return -EFAULT;
			}
			break;
		default:
			return -ENOTTY;
	}
	return SUCCESS;
}




//This function calls on demand of read request from seq_files
static int log_proc_show(struct seq_file *m, void *v){
	static int i;
	//Then for each item in the linkedlist we print the buffer and go for the next item ;)
	printk(KERN_INFO "USBLOGGER: %d USB Devices Has Logged in The USBLOGGER Module\n", linkedlist_count);
	spin_lock(&linkedlist_usage_spinlock);
	list_list = list_tail;
	for(i=1; i<=linkedlist_count && list_list!=NULL; i++){
		seq_printf(m, "%d: %s\n", i, list_list->buffer);
		list_list = list_list->next;
	}
	spin_unlock(&linkedlist_usage_spinlock);
	return SUCCESS;
}


//This is where system functionallity triggers every time some process try to read from our proc entry
static int log_proc_open(struct inode *inode, struct file *file){
	//Check if anyone else wanted to open the device first
	if(atomic_read(&module_usage_flag)){
		//If user process can not wait for respose and device was opend by another process, just reject it
		if(file->f_flags & O_NONBLOCK)
			return -EBUSY;
		//If device previously opend, the the user process have to wait until the condition goes wrong
		if(wait_event_interruptible(our_queue, (atomic_read(&module_usage_flag) == 1)))
			return -ERESTARTSYS;
	}
	//Now increment the open flag by one
	atomic_inc(&module_usage_flag);
	//Eachtime you open the entry point, infact you are using the device, so you have to
	//count the references to it, in order to when you want to release it, you could safely release
	//the device with reference count of zero
	//So we increase the reference count using try_module_get
	try_module_get(THIS_MODULE);
	//And now we will process the request
	return single_open(file, log_proc_show, NULL);
}


//Each time you release the /dev entry after read or write somthing from and to /dev entry
//This function have to adjust the reference count and does its job
static int log_proc_release(struct inode *inode, struct file *file){
	//decrement the open flag by one so other users could go through the opening condition
	atomic_dec(&module_usage_flag);
	//Wake up all processes that waited for this module
	wake_up_interruptible(&our_queue);
	//When you release the entry point, that means you have finished with the device so
	//decrese the reference count wit module_put
	module_put(THIS_MODULE);
	//And finally release the file
	return single_release(inode, file);
}








//This function calls on demand of read request from seq_files
static int dev_proc_show(struct seq_file *m, void *v){
	static int occupied_space, i;
	static char peek_queue_buffer[MAX_BUF_LEN];

	//Here we have to obtain how many items left in the queue
	//So we calculate the difference between actual queue size (in bytes) and available space (in bytes too)
	//Then we divide the result by our data quantum (in bytes), the result means the number of items
	//In This sample, this value has stored in liklist_count all the time
	spin_lock(&queue_usage_spinlock);
	occupied_space = (kfifo_size(&dev_queue) - kfifo_avail(&dev_queue)) / sizeof(dev_queue_type);
	//Then for each item, we pop it, print the result and insert in at the end of the queue ;)
	for(i=0; i<occupied_space; i++){
		kfifo_out(&dev_queue, peek_queue_buffer, sizeof(dev_queue_type));
		seq_printf(m, "%d: %s\n", i, peek_queue_buffer);
		kfifo_in(&dev_queue, peek_queue_buffer, sizeof(dev_queue_type));
	}
	spin_unlock(&queue_usage_spinlock);

	return SUCCESS;
}


//Each time user try to echo something or otherwise write anything to the /dev entry, this function does the job
static ssize_t dev_proc_write(struct file *file, const char *buffer, size_t length, loff_t * off){
	if (length > MAX_BUF_LEN)
		queue_buffer_size = MAX_BUF_LEN;
	else
		queue_buffer_size = length;

	//write data to the buffer
	spin_lock(&queue_usage_spinlock);
	if(raw_copy_from_user(queue_buffer, buffer, queue_buffer_size))
		return -EFAULT;

	if(!kfifo_is_full(&dev_queue))
		kfifo_in(&dev_queue, queue_buffer, sizeof(dev_queue_type));
	spin_unlock(&queue_usage_spinlock);

	//The function returns wrote charachters count
	return queue_buffer_size;
}


//This is where system functionallity triggers every time some process try to read from our proc entry
static int dev_proc_open(struct inode *inode, struct file *file){
	//Check if anyone else wanted to open the device first
	if(atomic_read(&module_usage_flag)){
		//If user process can not wait for respose and device was opend by another process, just reject it
		if(file->f_flags & O_NONBLOCK)
			return -EBUSY;
		//If device previously opend, the the user process have to wait until the condition goes wrong
		if(wait_event_interruptible(our_queue, (atomic_read(&module_usage_flag) == 1)))
			return -ERESTARTSYS;
	}
	//Now increment the open flag by one
	atomic_inc(&module_usage_flag);
	//Eachtime you open the entry point, infact you are using the device, so you have to
	//count the references to it, in order to when you want to release it, you could safely release
	//the device with reference count of zero
	//So we increase the reference count using try_module_get
	try_module_get(THIS_MODULE);
	//And now we will process the request
	return single_open(file, dev_proc_show, NULL);
}


//Each time you release the /dev entry after read or write somthing from and to /dev entry
//This function have to adjust the reference count and does its job
static int dev_proc_release(struct inode *inode, struct file *file){
	//decrement the open flag by one so other users could go through the opening condition
	atomic_dec(&module_usage_flag);
	//Wake up all processes that waited for this module
	wake_up_interruptible(&our_queue);
	//When you release the entry point, that means you have finished with the device so
	//decrese the reference count wit module_put
	module_put(THIS_MODULE);
	//And finally release the file
	return single_release(inode, file);
}




//This function will distinguish between various device classes
static char identify_device_class_type(__u8 device_class){
	switch(device_class){
		case USB_CLASS_AUDIO:
			return 'A';
			break;
		case USB_CLASS_COMM:
			return 'C';
			break;
		case USB_CLASS_HID:
			return 'D';
			break;
		case USB_CLASS_PRINTER:
			return 'P';
			break;
		case USB_CLASS_HUB:
			return 'H';
			break;
		case USB_CLASS_VIDEO:
			return 'V';
			break;
		case USB_CLASS_MASS_STORAGE:
			return 'S';
			break;
		case USB_CLASS_WIRELESS_CONTROLLER:
			return 'W';
			break;
		default:
			return 'N';
		}
}



//This function returns a sophisticated timefor a USB event
static void get_log_time(char *usb_time){
	//This is how we are going to get these times
	//First, we use current_kernel_time to obtain current time of system
	//which will be used to calculate times for GMT value
	struct timespec my_timeofday_gmt = current_kernel_time();

	//These variables will use to create a more human readable output
	int hour_gmt, minute_gmt, second_gmt;

	//Now we only use the second fragment of timespec structs
	second_gmt = (int) my_timeofday_gmt.tv_sec;
	hour_gmt = (second_gmt / 3600) % 24;
	minute_gmt = (second_gmt / 60) % 60;
	second_gmt %= 60;

	//Now we create a sharp simple output
	sprintf(usb_time, "%d:%d:%d",hour_gmt, minute_gmt, second_gmt);
}



static int usb_notify(struct notifier_block *self, unsigned long action, void *dev){
	char event_time[9] = "";
	char notifier_string[3] = "";
	
	//Get usb_device struct from the passing data
	struct usb_device *usbdev = (struct usb_device *) dev;
	if(!usbdev)
		return NOTIFY_DONE;
		
	
	get_log_time(event_time);
	//get_log_date(event_date);
		
	//Decide on different actions
	switch(action){
		case USB_DEVICE_ADD:
			strcpy(notifier_string, "DA");
			break;
		case USB_DEVICE_REMOVE:
			strcpy(notifier_string, "DR");
			break;
		case USB_BUS_ADD:
			strcpy(notifier_string, "BA");
			break;
		case USB_BUS_REMOVE:
			strcpy(notifier_string, "BR");
			break;
		default:
			return NOTIFY_DONE;
	
	}
	
	sprintf(linkedlist_buffer, "%04X:%04X %s%c %s", usbdev->descriptor.idVendor, usbdev->descriptor.idProduct, notifier_string, identify_device_class_type(usbdev->descriptor.bDeviceClass), event_time);
	
	//Store the buffer in the queue
	printk(KERN_ALERT "USBLOGGER: %s\n", linkedlist_buffer);
	spin_lock(&linkedlist_usage_spinlock);
	
	if(linkedlist_count == linkedlist_size){
		list_comm = list_head;
		list_head = list_comm->perv;
		list_head->next = NULL;
		mempool_free(list_comm, log_pool);
		linkedlist_count--;
	}

	list_element = mempool_alloc(log_pool, GFP_HIGHUSER);
	strncpy(list_element->buffer, linkedlist_buffer, LOG_BUF_LEN);
	
	if(linkedlist_count == 0){
		list_head = list_tail = list_element;
		list_head->next = list_tail->next = list_element->next = NULL;
		list_head->perv = list_tail->perv = list_element->perv = NULL;
		}
	else{
		list_element->next = list_tail;
		list_element->perv = NULL;
		list_tail->perv = list_element;
		list_tail = list_element;
	}
	linkedlist_count++;
	
	//Search for the Blocked Device in the Kfifo
	
	spin_unlock(&linkedlist_usage_spinlock);
	
	return NOTIFY_OK;
}



//Using notifiers for identifying actions on usb
static struct notifier_block usb_nb = {
	.notifier_call = usb_notify, //This fuction will call whenever the notifier triggers
};


//Struct file_operations is the key to the functionality of the module
//functions that defined here are going to add to the kernel functionallity
//in order to respond to userspace access demand to the correspond /proc entry
static const struct file_operations log_fops = {
	.owner = THIS_MODULE,
	.open = log_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = log_proc_release,
	.unlocked_ioctl = log_proc_ioctl, //This fuction will call whenever the ioctl command recieved from the user
};

static const struct file_operations dev_fops = {
	.owner = THIS_MODULE,
	.open = dev_proc_open,
	.read = seq_read,
	.write = dev_proc_write,
	.llseek = seq_lseek,
	.release = dev_proc_release,
	.unlocked_ioctl = dev_proc_ioctl, //This fuction will call whenever the ioctl command recieved from the user
};



//You sould clean up the mess before exiting the module
static void usb_logger_exit(void){
	//First, the notifier as the main function call should be unregistered
	usb_unregister_notify(&usb_nb);
	
	//Second, We remove the proc interface, so the users could not demand for this module's functionality
	if(dev_proc_file)
		remove_proc_entry("blockedusb", NULL);

	if(log_proc_file)
		remove_proc_entry(MODULE_NAME, NULL);	
	//Third, it is time for other data structures to be unregistered
	//Free the queue allocated memory
	kfifo_free(&dev_queue);

	if(log_pool)
		mempool_destroy(log_pool);
		
	if(our_cache)
		kmem_cache_destroy(our_cache);

	printk(KERN_INFO "USBLOGGER: %s module has been unregistered.\n", MODULE_NAME);
	//The cleanup_module function doesn't need to return any value to the rest of the Kernel
}


//Your module's entry point
static int usb_logger_init(void){
	//First we have to register some data structure that might be used by the module
	//Registering and initialising a kfifo queue
	if(kfifo_alloc(&dev_queue, PAGE_SIZE, GFP_USER)){
		printk(KERN_ALERT "USBLOGGER: Kfifo Registration Failure.\n");
		usb_logger_exit();
		return -ENOMEM;
	}
	
	DEFINE_KFIFO(dev_queue, dev_queue_type, 16);
	
	//Now we have to create a lookaside cache and memeory pool for the log system
	our_cache = kmem_cache_create("our_lookaside_cache", sizeof(list_element), 0, SLAB_HWCACHE_ALIGN, NULL);
	if(!our_cache){
		printk(KERN_ALERT "USBLOGGER: Lookaside Cache Registration Failure.\n");
		usb_logger_exit();
		//Because of this fact that lookaside cache will obtain memory form system RAM, this error means the lack of enough memory
		return -ENOMEM;
	}

	log_pool = mempool_create(32, mempool_alloc_slab, mempool_free_slab, our_cache);
	if(!log_pool){
		printk(KERN_ALERT "USBLOGGER: Memory Pool Registration Failure.\n");
		usb_logger_exit();
		//Because of this fact that lookaside cache will obtain memory form system RAM, this error means the lack of enough memory
		return -ENOMEM;
	}
	
	linkedlist_count = 0;
	linkedlist_size = 32;
	list_head = NULL;
	list_tail = NULL;
	list_comm = NULL;
	list_element = NULL;
	
	//Registering a spinlock
	spin_lock_init(&queue_usage_spinlock);
	spin_lock_init(&linkedlist_usage_spinlock);
	//Registering a waitqueue
	init_waitqueue_head(&our_queue);
	
	
	//Then, we should register the interfaces and notifier
	log_proc_file = proc_create(MODULE_NAME, 0644 , NULL, &log_fops);
	//Put an error message in kernel log if cannot create proc entry
	if(!log_proc_file){
		printk(KERN_ALERT "USBLOGGER: Proc File Registration Failure.\n");
		//Because of this fact that procfs is a RAM filesystem, this error means the lack of enough memory
		return -ENOMEM;
	}


	dev_proc_file = proc_create("blockedusb", 0644 , NULL, &dev_fops);
	//Put an error message in kernel log if cannot create proc entry
	if(!dev_proc_file){
		printk(KERN_ALERT "USBLOGGER: Proc File Registration failure.\n");
		//Because of this fact that procfs is a ram filesystem, this error means the lack of enough memory
		return -ENOMEM;
	}
	
				
	//At last it is time to register our notifier
	usb_register_notify(&usb_nb);

	//Notify the user in the Kernel log of the module successful initialisation
	printk(KERN_INFO "USBLOGGER: %s module has been registered.\n", MODULE_NAME);
	
	//The init_module should return a value to the rest of kernel that asure
	//them to its successfully registration of its functionality
	return SUCCESS;
}


//Now we need to define init-module and cleanup_module aliases
module_init(usb_logger_init);
module_exit(usb_logger_exit);
