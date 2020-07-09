#include <stdio.h>
#include <stdlib.h>

int main() {
        int n = 3;              //size of matrices
        int m1[n][n];
        int m2[n][n];
        int result[n][n];
        int i, j, k;            //vars for indexes
        FILE *file1, *file2;
        int temp[n*n];

        //fill matrix1
        file1 = fopen ( "matrix", "r"); //read in file

        //exit if opening file fails
        if ( file1 == NULL ) { 
                printf( "Error opening file.\n" );
                exit (0);
        }   

        i=0;
        while ( !feof (file1) ) { 
                fscanf( file1, "%d", &temp[i] );
                i++;
        }   


        //close file
        fclose(file1);

        //enter values into matrix
        k = 0;
        for ( i=0; i<n; i++ ) { 
                for ( j=0; j<n; j++ ) { 
                        m1[i][j] = temp[k];
                        k++;
                }   
        }   

        //fill matrix2
        file2 = fopen ( "matrix2", "r");        //read in file

        //exit if opening file fails
        if ( file2 == NULL ) { 
                printf( "Error opening file.\n" );
                exit (0);
        }   

        i=0;
        while ( !feof (file2) ) { 
                fscanf( file2, "%d", &temp[i] );
                i++;
        }   

        //close file
        fclose(file2);


        //enter values into matrix
        k = 0;
        for ( i=0; i<n; i++ ) { 
                for ( j=0; j<n; j++ ) { 
                        m2[i][j] = temp[k];
                        k++;
                }
        }
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

        //multiply matrices
        for ( i=0; i<n; i++ ) {
                for ( j=0; j<n; j++ ) {
                        result[i][j] = 0;
                        for ( k=0; k<n; k++ ) {
                                result[i][j] += m1[i][k] * m2[k][j];
                        }
                }
        }

        printf("results:\n");
        for ( i=0; i<n; i++ ) {
                for ( j=0; j<n; j++ ) {
                        printf("%d\t", result[i][j]);
                }
                printf("\n");
        }
        return 0;
}
 

