#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

int main() {
        struct timeval start,stop;

        int n = 700;              //size of matrices
        int m1[n][n];
        int m2[n][n];
        int result[n][n];
        int i, j, k;            //vars for indexes
        FILE *file;
        int temp[n*n];

        //fill matrix1
        file = fopen ( "matrix", "r");  //read in file

        //exit if opening file fails
        if ( file == NULL ) {
                printf( "Error opening file.\n" );
                exit (0);
        }

        i=0;
        while ( !feof (file) ) {
                fscanf( file, "%d", &temp[i] );
                i++;
        }

        //close file
        fclose(file);

        //enter values into matrix
        k = 0;
        for ( i=0; i<n; i++ ) {
                for ( j=0; j<n; j++ ) {
                        m1[i][j] = temp[k];
                        k++;
                }
        }

        //fill matrix2
        file = fopen ( "matrix2", "r"); //read in file

        //exit if opening file fails
        if ( file == NULL ) {
                printf( "Error opening file.\n" );
                exit (0);
        }
        i=0;
        while ( !feof (file) ) {
                fscanf( file, "%d", &temp[i] );
                i++;
        }

        //close file
        fclose(file);

        //enter values into matrix
        k = 0;
        for ( i=0; i<n; i++ ) {
                for ( j=0; j<n; j++ ) {
                        m2[i][j] = temp[k];
                        k++;
                }
        }
/*
        //print matrices
        printf("matrix1:\n");
        for ( i=0; i<n; i++ ) {
                for ( j=0; j<n; j++ ) {
                        printf("%d\t", m1[i][j]);
                }
                printf("\n");
        }

        printf("matrix2:\n");
        for ( i=0; i<n; i++ ) {
                for ( j=0; j<n; j++ ) {
                        printf("%d\t", m2[i][j]);
                }
                printf("\n");
        }
*/
        file = fopen ( "results", "w"); //read in file

        //exit if opening file fails
        if ( file == NULL ) {
                printf( "Error opening file.\n" );
                exit (0);
        }

        gettimeofday( &start, NULL );
        //multiply matrices
        for ( i=0; i<n; i++ ) {
                for ( j=0; j<n; j++ ) {
                        result[i][j] = 0;
                        for ( k=0; k<n; k++ ) {
                                result[i][j] += m1[i][k] * m2[k][j];
                        }
                                fprintf(file, "%d ", result[i][j]);
                }
                fprintf(file, "\n");
        }
        gettimeofday( &stop, NULL );

        fclose(file);
/*
        printf("stop.tv_sec = %ld\n", stop.tv_sec);
        printf("start.tv_sec = %ld\n", start.tv_sec);
        printf("stop.tv_usec = %d\n", stop.tv_usec);
        printf("stop.tv_usec = %d\n", start.tv_usec);
        //store results in to file
*/
        printf("For a naive algorithm for dense matrix multiplication on a two 700x700 matrices, %lf seconds elapsed.\n",
                ((stop.tv_sec - start.tv_sec) + (stop.tv_usec - start.tv_usec)/1000000.0 ));
        return 0;
}

