//These are our ioctl definition
#define LOG_MAGIC 'Q'
#define LOG_IOC_MAXNR 8
#define IOCTL_LOG_RESET 	_IO(LOG_MAGIC, 0)
#define IOCTL_LOG_COUNT 	_IOR(LOG_MAGIC, 1, int)
#define IOCTL_LOG_SPACE 	_IOR(LOG_MAGIC, 2, int)
#define IOCTL_LOG_SIZE 	_IOR(LOG_MAGIC, 3, int)
#define IOCTL_LOG_FULL 		_IOR(LOG_MAGIC, 4, int)
#define IOCTL_LOG_EMPTY 	_IOR(LOG_MAGIC, 5, int)
#define IOCTL_LOG_ESIZE 	_IOR(LOG_MAGIC, 6, int)
#define IOCTL_LOG_DELETE 	_IOW(LOG_MAGIC, 7, int)


#define DEV_MAGIC 'T'
#define DEV_IOC_MAXNR 7
#define IOCTL_DEV_RESET 	_IO(DEV_MAGIC, 0)
#define IOCTL_DEV_COUNT 	_IOR(DEV_MAGIC, 1, int)
#define IOCTL_DEV_SPACE 	_IOR(DEV_MAGIC, 2, int)
#define IOCTL_DEV_SIZE 	_IOR(DEV_MAGIC, 3, int)
#define IOCTL_DEV_FULL 		_IOR(DEV_MAGIC, 4, int)
#define IOCTL_DEV_EMPTY 	_IOR(DEV_MAGIC, 5, int)
#define IOCTL_DEV_ESIZE 	_IOR(DEV_MAGIC, 6, int)

