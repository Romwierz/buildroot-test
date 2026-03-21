/* Struktura opisująca jeden emulowany akcelerator */
struct fir_dev {
	struct semaphore lock;
	int delay;
} __attribute__((packed));

/* Struktura opisująca jedno użycie akceleratora */
struct fir_file {
    int ntaps;
    int first;
    struct fir_dev * dev_ctx;
    int16_t * samples;
    int16_t * coeffs;
} __attribute__((packed));
