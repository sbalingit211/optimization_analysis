#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include "../msr-safe/msr_safe.h"
#include <math.h>
#include <sys/time.h>
#include <pthread.h>
#define IA32_PERF_CTL ( 0x199 )
#define IA32_PERF_STATUS ( 0x198 )
#define IA32_PERF_GLOBAL_CTRL ( 0x38f )
#define IA32_FIXED_CTR0 ( 0x309 )
#define IA32_PERFEVTSEL0 ( 0x186 )
#define IA32_PMC0 ( 0xC1 )
#define TOTAL_CPUS 32
#define TOTAL_OPS_PER_CPU 2
#define TOTAL_SETUP_OPS_PER_CPU ( TOTAL_CPUS * 4 )
#define TOTAL_FINAL_OPS_PER_CPU ( TOTAL_CPUS * 2 )
#define TOTAL_OPS ( TOTAL_CPUS * TOTAL_OPS_PER_CPU )
#define CPU_FREQ 2.6

struct PerfInfo {
	double mperf;
	double aperf;
	double thread_freq;
	double instr;
	double operating_point;
};

enum {
	BARRIER_INIT = 0,
	BARRIER_DELTA = 1,
	NUM_BARRIERS = 2
};

static pthread_barrier_t barrier[ NUM_BARRIERS ];
//static double total = 0;
struct PerfInfo perf_info[ TOTAL_CPUS ];

/*******************************************************
 *************	FUNCTION DECLARATIONS ******************
 ******************************************************/

static int loadMSR();

void closeMSR( int fd );

void getFreq( int fd, struct msr_batch_array *batch );

void SetupProgCounters( int fd, struct msr_batch_array *batch );
void getOPBeforeCalc( int fd, struct msr_batch_array *batch );
void getOPAfterCalc( int fd, struct msr_batch_array *batch );

void* threadLoop( void *thread_num );

void calculatePerf( struct msr_batch_op start_op[], struct msr_batch_op end_op[] );

void calculateOP( struct msr_batch_op ctl_op[], struct msr_batch_op status_op[] );

void calculateInstr( struct msr_batch_op final_counter_op[] );

void getPerformance( int fd, struct msr_batch_array *batch );

void printInfo( struct msr_batch_op start_op[], struct msr_batch_op end_op[] );

void printOP( struct msr_batch_op ctl_op[], struct msr_batch_op status_op[] );

void printPerformance( struct msr_batch_op final_counter_op[] );

/*******************************************************
 *************		MAIN		****************
 ******************************************************/
//int main( int argc, char *argv[] ) {
int foo;

int main() {
	uint64_t t;	//for thread ID

	//set up thread work
	uint32_t num_threads = 30;
//	num_threads = atoi( argv[ NUM_THREADS_INDEX ] );
	pthread_t threads[ num_threads ];

	int fd;
	struct msr_batch_array batch_freq, batch_performance;
	struct msr_batch_op freq_start_op[ TOTAL_CPUS*2 ], freq_end_op[ TOTAL_CPUS*2 ];
	struct msr_batch_op perf_ctl_op[ TOTAL_CPUS ], perf_status_op[ TOTAL_CPUS ];
	struct msr_batch_op counter_setup[ TOTAL_SETUP_OPS_PER_CPU ], final_counter[ TOTAL_FINAL_OPS_PER_CPU ];

	//open MSR
	fd = loadMSR();

	//initialize barrier for pthread work
	for( t=0; t<NUM_BARRIERS; t++ ) {
		assert( ! pthread_barrier_init( &barrier[t], NULL, num_threads ) );
	}


	//create threads and disperse to ahve them each complete their set of calculations
	for( t=0; t<num_threads; t++ ) {
		if( t==num_threads-1 ) {
			
			//read MPERF/APERF before incrementing
			batch_freq.numops = TOTAL_CPUS*2;
			batch_freq.ops = freq_start_op;
			getFreq( fd, &batch_freq );

			//reset global and fixed ctrl MSRs
			batch_performance.numops = TOTAL_SETUP_OPS_PER_CPU;
			batch_performance.ops = counter_setup;
			SetupProgCounters( fd, &batch_performance );
			
			//get voltage CPU is currently at before calculation
			batch_freq.numops = TOTAL_CPUS;
			batch_freq.ops = perf_ctl_op;
			getOPBeforeCalc( fd, &batch_freq );
		}
		assert( ! pthread_create( &threads[t], NULL, threadLoop, (void*)t ) );
	}
	
	//end thread work
	for( t=0; t<num_threads; t++ ) {
		pthread_join( threads[t], NULL );
		if( t==0 ) {

			//read current operating point after calculation
			batch_freq.ops = perf_status_op;
			getOPAfterCalc( fd, &batch_freq );

			//read MPERF/APERF after incrementing
			batch_freq.numops = TOTAL_CPUS*2;
			batch_freq.ops = freq_end_op;
			getFreq( fd, &batch_freq );

			//read PMC
			batch_performance.numops = TOTAL_FINAL_OPS_PER_CPU;
			batch_performance.ops = final_counter;
			getPerformance( fd, &batch_performance );
		}
	}

	for( t=0; t<NUM_BARRIERS; t++ ) {
		assert( ! pthread_barrier_destroy( &barrier[t] ) );
	}


	//calculate MPERF/APERF
	calculatePerf( freq_start_op, freq_end_op );
	calculateOP( perf_ctl_op, perf_status_op );


	calculateInstr( final_counter );
	
	//close MSR
	closeMSR( fd );

	//print MPERF/APERF info
	//printInfo( freq_start_op, freq_end_op );
	printOP( perf_ctl_op, perf_status_op );
	//print performance
	//printPerformance( final_counter );

	pthread_exit( NULL );
}


/*******************************************************
 *************	FUNCTION DEFINITIONS ******************
 ******************************************************/

static int loadMSR() {
	int fd;
	fd = open( "/dev/cpu/msr_batch", O_RDWR );
	assert( fd != -1 );

	return fd;
}

void closeMSR( int fd ) {
	int c;
	
	c = close( fd );
	assert( c != -1 );
}

void SetupProgCounters( int fd, struct msr_batch_array *batch ) {
	
	int i, j=0, rc;

	for( i=0; i<TOTAL_CPUS; i++, j++ ) {
		batch->ops[j].cpu = i;
		batch->ops[j].isrdmsr = 0;
		batch->ops[j].err = 0;
		batch->ops[j].msr = IA32_PERF_GLOBAL_CTRL; //IA32_PERF_GLOBAL_CTRL MSR
		batch->ops[j].msrdata = 0;
		batch->ops[j].wmask = 0;
	}

	for( i=0; i<TOTAL_CPUS; i++, j++ ) {
		batch->ops[j].cpu = i;
		batch->ops[j].isrdmsr = 0;
		batch->ops[j].err = 0;
		batch->ops[j].msr = IA32_PERFEVTSEL0;
		batch->ops[j].msrdata = (1<<22) | (1<<16) | (0xC4);
		batch->ops[j].wmask = 0;
	}

	for( i=0; i<TOTAL_CPUS; i++, j++ ) {
		batch->ops[j].cpu = i;
		batch->ops[j].isrdmsr = 0;
		batch->ops[j].err = 0;
		batch->ops[j].msr = IA32_PMC0;
		batch->ops[j].msrdata = 0;
		batch->ops[j].wmask = 0;
	}

	for( i=0; i<TOTAL_CPUS; i++, j++ ) {
		batch->ops[j].cpu = i;
		batch->ops[j].isrdmsr = 0;
		batch->ops[j].err = 0;
		batch->ops[j].msr = IA32_PERF_GLOBAL_CTRL;
		batch->ops[j].msrdata = 0x1;
		batch->ops[j].wmask = 0;
	}

	j=TOTAL_CPUS*3;
	printf( "update last bit in PERF_GLOBAL_CTRL:  ");
	for( i=0; i<TOTAL_CPUS; i++, j++ ) {
		printf( " %" PRIu64 " ", (uint64_t) batch->ops[j].msrdata );
	}
	printf( "\n" );
	//printf( "j: %d \n", j );


	rc = ioctl( fd, X86_IOC_MSR_BATCH, batch );
	/*
	j=0;
	while (j<TOTAL_SETUP_OPS_PER_CPU) {
		printf( "PERF_GLOBAL_CTRL:\n");
		for( i=0; i<TOTAL_CPUS; i++, j++ ) {
			printf( "\t CPU: %" PRIu64 "\t ERR: %" PRIu64 "\t MSR: %" PRIu64 "\t data: %" PRIu64 "\n", (uint64_t) batch->ops[j].cpu, (uint64_t) batch->ops[j].err, (uint64_t) batch->ops[j].msr, (uint64_t) batch->ops[j].msrdata );
		}
	printf( "\n" );

		printf( "PERFEVTSEL0:\n");
		for( i=0; i<TOTAL_CPUS; i++, j++ ) {
			printf( "\t CPU: %" PRIu64 "\t ERR: %" PRIu64 "\t MSR: %" PRIu64 "\t data: %" PRIu64 "\n", (uint64_t) batch->ops[j].cpu, (uint64_t) batch->ops[j].err, (uint64_t) batch->ops[j].msr, (uint64_t) batch->ops[j].msrdata );
		}
	printf( "\n" );

		printf( "PMC0:\n");
		for( i=0; i<TOTAL_CPUS; i++, j++ ) {
			printf( "\t CPU: %" PRIu64 "\t ERR: %" PRIu64 "\t MSR: %" PRIu64 "\t data: %" PRIu64 "\n", (uint64_t) batch->ops[j].cpu, (uint64_t) batch->ops[j].err, (uint64_t) batch->ops[j].msr, (uint64_t) batch->ops[j].msrdata );
		}
	printf( "\n" );

		printf( "PERF_GLOBAL_CTRL:\n");
		for( i=0; i<TOTAL_CPUS; i++, j++ ) {
			printf( "\t CPU: %" PRIu64 "\t ERR: %" PRIu64 "\t MSR: %" PRIu64 "\t data: %" PRIu64 "\n", (uint64_t) batch->ops[j].cpu, (uint64_t) batch->ops[j].err, (uint64_t) batch->ops[j].msr, (uint64_t) batch->ops[j].msrdata );
		}
	printf( "\n" );
	}
*/
	assert( rc != -1 );

}

void* threadLoop( void *thread_num ) {
	uint64_t t = (uint64_t) (thread_num);
	//uint64_t num = 0;
	 int i, j ;

	if( t == t ) {
		for( i=0; i<1500000; i++) {
			//fprintf( stdout, "i = %d, foo = %d\n", i, foo );
			foo += j*foo;
		}
//			num = pow( 34897652894/4502.2345234, 2 );
	}
	//printf( "thread: %2d \t num: $d \n", (int) t, num );
//	total += num;
//	printf( "thread: %2d \t total: %.4lf \n", (int) t, total );

//		printf( "thread %d here\n", t );
	pthread_exit( NULL );
}


void getOPBeforeCalc( int fd, struct msr_batch_array *batch ) {
	int i, rc;

	for( i=0; i<TOTAL_CPUS; i++ ) {
		batch->ops[i].cpu = i;
		batch->ops[i].isrdmsr = 1;
		batch->ops[i].err = 0;
		batch->ops[i].msr = IA32_PERF_CTL;
		batch->ops[i].msrdata = 0x1;
		batch->ops[i].wmask = 0;
	}
	rc = ioctl( fd, X86_IOC_MSR_BATCH, batch );
	assert( rc != -1 );
}

void getOPAfterCalc( int fd, struct msr_batch_array *batch ) {
	int i, rc;

	for( i=0; i<TOTAL_CPUS; i++ ) {
		batch->ops[i].cpu = i;
		batch->ops[i].isrdmsr = 1;
		batch->ops[i].err = 0;
		batch->ops[i].msr = IA32_PERF_STATUS;
		batch->ops[i].msrdata = 0x1;
		batch->ops[i].wmask = 0;
	}
	rc = ioctl( fd, X86_IOC_MSR_BATCH, batch );
	assert( rc != -1 );
}

void calculatePerf( struct msr_batch_op start_op[], struct msr_batch_op end_op[] ) {
	int i;

	for( i=0; i<(TOTAL_CPUS*2); i+=2 ) {
		perf_info[i/2].mperf = (end_op[i].msrdata - start_op[i].msrdata );
		perf_info[i/2].aperf = (end_op[i+1].msrdata - start_op[i+1].msrdata );
		perf_info[i/2].thread_freq = CPU_FREQ * (perf_info[i/2].aperf / perf_info[i/2].mperf );
	}
}


void calculateOP( struct msr_batch_op ctl_op[], struct msr_batch_op status_op[] ) {
	int i;

	for( i=0; i<TOTAL_CPUS; i++ ) {
		perf_info[i].operating_point = status_op[i].msrdata - ctl_op[i].msrdata;
	}
}

void calculateInstr( struct msr_batch_op final_counter_op[] ) {
	int i;

	for( i=0; i<TOTAL_CPUS; i++ ) {
		perf_info[i].instr = final_counter_op[TOTAL_CPUS+i].msrdata / perf_info[i].aperf;
	}
}

void getPerformance( int fd, struct msr_batch_array *batch ) {
	int i, j=0, rc;

	//zero out global_ctrl
	for( i=0; i<TOTAL_CPUS; i++, j++ ) {
		batch->ops[j].cpu = i;
		batch->ops[j].isrdmsr = 0;
		batch->ops[j].err = 0;
		batch->ops[j].msr = IA32_PERF_GLOBAL_CTRL; //IA32_PERF_GLOBAL_CTRL MSR
		batch->ops[j].msrdata = 0;
		batch->ops[j].wmask = 0;
	}

	//read PMC0
	for( i=0; i<TOTAL_CPUS; i++, j++ ) {
		batch->ops[j].cpu = i;
		batch->ops[j].isrdmsr = 1;
		batch->ops[j].err = 0;
		batch->ops[j].msr = IA32_PMC0;
		batch->ops[j].msrdata = 0;
		batch->ops[j].wmask = 0;
	}

	rc = ioctl( fd, X86_IOC_MSR_BATCH, batch );
	assert( rc != -1 );
}


void getFreq( int fd, struct msr_batch_array *batch ) {
	int i, rc;
	for( i=0; i<TOTAL_CPUS*2; i+=2 ) {
		//MPERF	
		batch->ops[i].cpu = i/2;
		batch->ops[i].isrdmsr = 1;
		batch->ops[i].err = 0;
		batch->ops[i].msr = 0xE7;
		batch->ops[i].msrdata = 0;
		batch->ops[i].wmask = 0;	//change bits here
		
		//APERF
		batch->ops[i+1].cpu = i/2;
		batch->ops[i+1].isrdmsr = 1;
		batch->ops[i+1].err = 0;
		batch->ops[i+1].msr = 0xE8;
		batch->ops[i+1].msrdata = 0;
		batch->ops[i+1].wmask = 0;	//change bits here
	}

	rc = ioctl( fd, X86_IOC_MSR_BATCH, batch );
	assert( rc != -1 );
}

void printInfo( struct msr_batch_op start_op[], struct msr_batch_op end_op[] ) {
	int i;

	//MPERF
	for( i=0; i<TOTAL_CPUS*2; i+=2 ) {
		//print MPERF info
		printf( "i: %d \t MPERF: START: %" PRIu64 "\t END: %" PRIu64 "\t DELTA: %.4lf \n", i/2, (uint64_t) start_op[i].msrdata, (uint64_t) end_op[i].msrdata, perf_info[i/2].mperf );
	}
	printf( "\n" );	

	//APERF
	for( i=0; i<TOTAL_CPUS*2; i+=2 ) {
		//print APERF info
		printf( "i: %d \t APERF: START: %" PRIu64 "\t END: %" PRIu64 "\t DELTA: %.4lf \n", i/2, (uint64_t) start_op[i+1].msrdata, (uint64_t) end_op[i+1].msrdata, perf_info[i/2].aperf );
	}
	printf( "\n" );
	//print frequency
	for( i=0; i<TOTAL_CPUS; i++ ) {
		printf( "i: %d \t FREQ: %.4lf INSTR: %.30lf \n", i, perf_info[i].thread_freq, perf_info[i].instr );
		//printf( "i: %d \t FREQ: %.4lf \n", i, perf_info[i].thread_freq );
	}
	printf( "\n" );

}

void printOP( struct msr_batch_op ctl_op[], struct msr_batch_op status_op[] ) {
	int i;

	for( i=0; i<TOTAL_CPUS; i++ ) {
		printf( "i: %2d \t START OP: %" PRIu64 "\t CURRENT OP: %" PRIu64 "\t DELTA: %.4lf \n", i, (uint64_t) ctl_op[i].msrdata, (uint64_t) status_op[i].msrdata, perf_info[i].operating_point );
	}
}


void printPerformance( struct msr_batch_op final_counter_op[] ) {
	int i;

	for( i=TOTAL_CPUS; i<TOTAL_FINAL_OPS_PER_CPU; i++ ) {
		printf( "i: %d \t INSTRUCTIONS RETIRED: %" PRIu64 "\n", i-TOTAL_CPUS, (uint64_t) final_counter_op[i].msrdata );
	}
	printf( "\n" );
}

