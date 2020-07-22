/* You are given a N x N array where the value in each
 * array represents an initial temperature.  There is
 * one heat source and one heat sink where the temperatures
 * remain constant.  The temperature of any location in the
 * 2d array at time t+1 is the average of the temperatures
 * of the surrounding locations (and the location itself)
 * at time t.
 *
 * Your job is to evalute this for sufficient timesteps that
 * the system reaches equilibrium (where the max change
 * between time t and t+1 for any cell is less than some delta.
 *
 * The standard way of doing this is to have two grids, one
 * for time t and one for time t+1.  After you've solved for
 * time t+1, swap the grids and continue.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <math.h>
#include <sys/time.h>
#include <assert.h>
#include <pthread.h>
#include "../msr-safe/msr_safe.h"

#define N (2000ULL)
#define NUMGRIDS (2ULL)
#define NUM_THREADS_INDEX 1

double grid[NUMGRIDS][N][N];
static uint64_t total_columns;

enum {
  BARRIER_INIT = 0,
  BARRIER_DELTA = 1,
  NUM_BARRIERS = 2
};

struct PerfFreqInfo {
	double mperf_freq;
	double mperf_delta;
	double aperf_freq;
	double aperf_delta;
};

static double elapsed_delta=0.0, elapsed_calc=0.0;
static double delta=0.0;

static pthread_barrier_t barrier[ NUM_BARRIERS ];


/**********************************************************************
****************        FUNCTION DECLARATIONS   ***********************
**********************************************************************/
void initializeGrid();

void printGrid( uint32_t grid_num );

void calculateAvg( uint32_t base_grid, uint32_t result_grid, uint64_t thread_num );

double calculateDelta();

void* threadLoop( void *thread_num );

void setMsrInfo( uint32_t num_threads, struct msr_batch_array *batch);

void calculatePerf( uint32_t num_threads, struct msr_batch_op start_op[], struct msr_batch_op end_op[], struct PerfFreqInfo perf_freq_info[] );

void printMsrInfo( uint32_t num_threads, struct msr_batch_op start_op[], struct msr_batch_op end_op[] );

void printPerf( uint32_t num_threads, struct PerfFreqInfo perf_freq_info[] );

/**********************************************************************
************************         MAIN           ***********************
**********************************************************************/
//int main() {
int main( int argc, char *argv[] ) {
        uint64_t t;
	uint32_t num_threads;
	num_threads = atoi( argv[ NUM_THREADS_INDEX ] );
        struct msr_batch_array batch_start, batch_end;
        struct msr_batch_op start_op[ num_threads*2 ], end_op[ num_threads*2 ];
        total_columns = ceil( N / ( double ) num_threads );
        pthread_t threads[ num_threads ];
	struct PerfFreqInfo perf_freq_info[ num_threads ];

        batch_start.numops = num_threads * 2;
        batch_end.numops = num_threads * 2;

        batch_start.ops = start_op;
        setMsrInfo( num_threads, &batch_start );

        for( t=0; t<NUM_BARRIERS; t++ ) {
                assert( ! pthread_barrier_init( &barrier[t], NULL, num_threads ) );
        }

        for( t=0; t<num_threads; t++ ) {
                assert( ! pthread_create( &threads[t], NULL, threadLoop, (void*)t ) );
        }

	batch_end.ops = end_op;
        setMsrInfo( num_threads, &batch_end );

        for( t=0; t<num_threads; t++ ) {
                pthread_join( threads[t], NULL );
        }

        for( t=0; t<NUM_BARRIERS; t++ ) {
                assert( ! pthread_barrier_destroy( &barrier[t] ) );
        }

        calculatePerf( num_threads, start_op, end_op, perf_freq_info );


        printMsrInfo( num_threads, start_op, end_op );
        printPerf( num_threads, perf_freq_info );

        pthread_exit( NULL );


}

/**********************************************************************
****************        FUNCTION DEFINITIONS    ***********************
**********************************************************************/

//function initializeGrid: initialize initial grid to the top-left value
//                         at -100.0 as a constant heat sink value, the
//                         bottom-right value at 100.0 as a constant
//                         heat source value, and all other positions
//                         as a double 0.0
void initializeGrid() {
        uint32_t grid_num, i, j;

        //initialize all positions to a double 0.0
        for( grid_num=0; grid_num < NUMGRIDS; grid_num++ ) {
                for ( i=0; i<N; i++ ) {
                        for ( j=0; j<N; j++ ) {
                                grid[grid_num][i][j] = 0.0;
                        }
                }
        }

	//heat sink
        grid[0][0][0] = -100.0;
	grid[1][0][0] = -100.0;

	//heat source
        grid[0][N-1][N-1] = 100.0;
        grid[1][N-1][N-1] = 100.0;
	
}

//function printGrid: print grid to screen
void printGrid( uint32_t grid_num ) {
        uint32_t i, j;

        printf("\n");

        for ( i=0; i<N; i++ ) {
                for ( j=0; j<N; j++ ) {
                        printf( "%6.2f  ", grid[grid_num][i][j]);
                }
                printf("\n");
        }
        printf("\n");
}

//function calculateAvg: calculate each position's value in the result
//                       matrix by finding the average of all surrounding
//                       positions (including itself)
void calculateAvg( uint32_t base_grid, uint32_t result_grid, uint64_t thread_num ) {
        uint32_t i,j, first_col=0, last_col=0;

        //non-edge, non-corner positions
        //identify set of columns to do calculations for
	if( thread_num==0 ) {
		first_col = 1;
		last_col = total_columns;
        }
	else {
        	first_col = thread_num * total_columns;
		if( thread_num < (N-1) ) {
			last_col = first_col + total_columns;
		}
		else {
			last_col = N-1;
		}
	}

        for( i=1; i<N; i++ ) {
		for( j=first_col; j<last_col; j++ ) {
                        grid[result_grid][i][j] = (
                                grid[base_grid][i-1][j-1] +
                                grid[base_grid][i-1][j  ] +
                                grid[base_grid][i-1][j+1] +
                                grid[base_grid][i  ][j-1] +
                                grid[base_grid][i  ][j  ] +
                                grid[base_grid][i  ][j+1] +
                                grid[base_grid][i+1][j-1] +
                                grid[base_grid][i+1][j  ] +
                                grid[base_grid][i+1][j+1] ) / 9.0;
                }
        }

        //thread 0 will calculate leftmost column, top row, and top-right corner
        if( thread_num==0 ) {
                //leftmost column, no corners
                for( i=1; i<N-1; i++ ) {
                        grid[result_grid][i][0] = (
                                grid[base_grid][i-1][0] +
                                grid[base_grid][i-1][1] +
                                grid[base_grid][i  ][0] +
                                grid[base_grid][i  ][1] +
                                grid[base_grid][i+1][0] +
                                grid[base_grid][i+1][1] ) / 6.0;
                }

//top row, no corners
                for( j=1; j<N-1; j++ ) {
                        grid[result_grid][0][j] = (
                                grid[base_grid][0][j-1] +
                                grid[base_grid][0][j  ] +
                                grid[base_grid][0][j+1] +
                                grid[base_grid][1][j-1] +
                                grid[base_grid][1][j  ] +
                                grid[base_grid][1][j+1] ) / 6.0;
                }

                //top-left corner is constant as as heat sink

                //top-right corner
                grid[result_grid][0][N-1] = (
                        grid[result_grid][0][N-1] +
                        grid[result_grid][0][N-2] +
                        grid[result_grid][1][N-1] +
                        grid[result_grid][1][N-2] ) / 4.0;

        }

        //thread N (or if only 1 thread, then thread 0 also) will calculate
        //      rightmost column, bottom row, and bottom-left corner
        if( thread_num==N || (thread_num==0 && thread_num==N) ) {
                //rightmost column, no corners
                for( i=1; i<N-1; i++ ) {
                        grid[result_grid][i][N-1] = (
                                grid[base_grid][i-1][N-2] +
                                grid[base_grid][i-1][N-1] +
                                grid[base_grid][i  ][N-2] +
                                grid[base_grid][i  ][N-1] +
                                grid[base_grid][i+1][N-2] +
                                grid[base_grid][i+1][N-1] ) / 6.0;
                }

                //bottom row, no corners
                for( j=1; j<N-1; j++ ) {
                        grid[result_grid][N-1][j] = (
                                grid[base_grid][N-2][j-1] +
                                grid[base_grid][N-2][j  ] +
                                grid[base_grid][N-2][j+1] +
                                grid[base_grid][N-1][j-1] +
                                grid[base_grid][N-1][j  ] +
                                grid[base_grid][N-1][j+1] ) / 6.0;
                }

//bottom-left corner
                grid[result_grid][0][N-1] = (
                        grid[result_grid][N-2][0] +
                        grid[result_grid][N-2][1] +
                        grid[result_grid][N-1][0] +
                        grid[result_grid][N-1][1] ) / 4.0;

                //bottom-right corner is constant as a heat source

        }


}

//function calculateDelta: returns greatest change between
//                          positions in result grid and base grid
double calculateDelta() {
        double init_delta=0.0, max_delta=0.0;
        uint32_t i,j;

        for( i=0; i<N; i++ ) {
                for( j=0; j<N; j++ ) {
                        delta = fabs( grid[0][i][j] - grid[1][i][j] );
                        if( init_delta > max_delta ) {
                                max_delta = init_delta;
                        }
                }
        }

        return max_delta;
}

//function threadLoop: called by each thread to calculate its portion
//                      of the matrix and store the time values needed
//                      to identify time length of its calculation
void* threadLoop( void* thread_num ) {
	uint64_t t = (uint64_t)(thread_num);
        double target_delta = 0.05;
        uint32_t count = 0;
        struct timeval init_start, init_stop, delta_start, delta_stop, calc_start, calc_stop;

        //initialize grid with thread 0
        if( t==0 ) {
                gettimeofday( &init_start, NULL );
                initializeGrid();
                gettimeofday( &init_stop, NULL );
                printf( "Elapsed (initial): %lf \t ", ( init_stop.tv_sec - init_start.tv_sec ) + (init_stop.tv_usec - init_start.tv_usec ) / 1000000.0 );
        }

        //hold up all threads until after thread 0 is done initializing
        pthread_barrier_wait( &barrier[BARRIER_INIT] );

        //combined calculation and stopping condition
        while( 1 ) {
                count++;
                gettimeofday( &calc_start, NULL );
                calculateAvg( !(count%2), !!(count%2), t );
                gettimeofday( &calc_stop, NULL );
                elapsed_calc += (calc_stop.tv_sec - calc_start.tv_sec) + (calc_stop.tv_usec - calc_start.tv_usec)/1000000.0;

                gettimeofday( &delta_start, NULL );
                if( t==0 ) {
                        delta = calculateDelta();
                }
                gettimeofday( &delta_stop, NULL );
                elapsed_delta += (delta_stop.tv_sec - delta_start.tv_sec) + (delta_stop.tv_usec - delta_start.tv_usec)/1000000.0;


//force everyone to wait until thread 0 has written to the global delta variable
                pthread_barrier_wait( &barrier[BARRIER_DELTA] );

                if( delta < target_delta ) {
                        break;
                }
        }

        if( t==0 ) {
                printf( "Elapsed (Delta): %lf \t Elapsed (Calculation): %lf \n", elapsed_delta, elapsed_calc );
                //printGrid(0);
        }

        pthread_exit( NULL );
}

//function setMsrInfo:
void setMsrInfo( uint32_t num_threads, struct msr_batch_array *batch) {
	uint64_t i, rc;
	int fd = open( "/dev/cpu/msr_batch", O_RDWR );
	assert( fd != -1 );

  for (i = 0; i < (num_threads*2); i+=2 ) {
    // mperf
    batch->ops[i].cpu = i/2;
    batch->ops[i].isrdmsr = 1;
    batch->ops[i].err = 0;
    batch->ops[i].msr = 0xE7;
    batch->ops[i].msrdata = 0;
    batch->ops[i].wmask = 0;

    // aperf
    batch->ops[i+1].cpu = i/2;
    batch->ops[i+1].isrdmsr = 1;
    batch->ops[i+1].err = 0;
    batch->ops[i+1].msr = 0xE8;
    batch->ops[i+1].msrdata = 0;
    batch->ops[i+1].wmask = 0;
  }

	rc = ioctl( fd, X86_IOC_MSR_BATCH, batch );
	assert( rc != -1 );
	close( fd );
/*	
	for( i=0; i< (num_threads*2); i+=2 ) {
		printf("%" PRIu64 " %" PRIu64 "\n", batch->ops[i], batch->ops[i+2] );
	}
	printf("\n");
*/
}

//function calculatePerf: calculates mperf and aperf
void calculatePerf( uint32_t num_threads, struct msr_batch_op start_op[], struct msr_batch_op end_op[], struct PerfFreqInfo perf_freq_info[] ) {
        uint32_t i;

        for( i=0; i < (num_threads*2); i+=2 ) {
                perf_freq_info[i/2].mperf_delta = ( end_op[i].msrdata - start_op[i].msrdata ) / elapsed_delta / 1000000000.0;
                perf_freq_info[i/2].aperf_delta = ( end_op[i+1].msrdata - start_op[i+1].msrdata ) / elapsed_delta / 1000000000.0;
        }
}

//function printMsrInfo: print msr information to terminal
void printMsrInfo( uint32_t num_threads, struct msr_batch_op start_op[], struct msr_batch_op end_op[] ) {
        uint32_t i;

	//print mperf data
        for( i=0; i < (num_threads*2); i+=2 ) {
                printf( "THREAD: %2d \t MPERF: START: %" PRIu64 "\t END: %" PRIu64 "\t DELTA: %" PRIu64 " \n", i/2, (uint64_t)start_op[i].msrdata, (uint64_t)end_op[i].msrdata, (uint64_t) end_op[i].msrdata - (uint64_t)start_op[i].msrdata );
	}
	printf( "\n\n" );

	//print aperf data
        for( i=0; i < (num_threads*2); i+=2 ) {
                printf( "THREAD: %2d \t APERF: START: %" PRIu64 "\t END: %" PRIu64 "\t DELTA: %" PRIu64 " \n", i/2, (uint64_t)start_op[i+1].msrdata, (uint64_t)end_op[i+1].msrdata, (uint64_t) end_op[i+1].msrdata - (uint64_t)start_op[i+1].msrdata );
        }
        printf("\n");
}

//function printPerf: print mperf and aperf information to terminal
void printPerf( uint32_t num_threads, struct PerfFreqInfo perf_freq_info[] ) {
        uint32_t i;

        for( i=0; i<num_threads; i++ ) {
                printf( "THREAD: %2d \t MPERF FREQ: %.4lf \t APERF FREQ: %.4lf \n", i,  perf_freq_info[i].mperf_delta, perf_freq_info[i].aperf_delta );
        }
        printf( "\n" );
}

