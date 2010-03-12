#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <string.h>
#include "pvm3.h" /* PVM 3 include file */

#define MAXPROCS 16 /* node programok max szama */
#define TAG_N 111 /* N uzenettipus */
#define TAG_SUM 222 /* SUM uzenettipus */
#define TAG_TIDS 333 /* TIDS uzenettipus */
#define DIMX 128 /* felbontas X iranyban */
#define DIMY 128 /* felbontas Y iranyban */

int _argc;
char **_argv;

char CM[3][3] =
{0, 1, 0,
 1,-4, 1,
 0, 1, 0}; /* konvolucios matrix */
char inp[DIMX][DIMY], oup[DIMX][DIMY]; /* be es kimeneti tombok */

main(int argc, char** argv)
{
	_argc = argc;
	_argv = argv;
	float err, sum, w, x;
	int N, M, info, mynum, nprocs, tids[MAXPROCS + 1];
	void startup(int *pmynum, int *pnprocs, int tids[]); 

	/* 
	 * Minden peldany meghivja a startup rutint,
	 * hogy megkapja a peldanyok szamat, es a taszk azonositokat,
	 * illetve a sajat azonositojat. Ha ez 0 akkor o az elso peldany.
	 */
	startup(&mynum, &nprocs, tids);

	/* Szamitas. M = nprocs + 1 peldany van */
	register i, j, k, l;
	float unit = (float)DIMY / (float)(nprocs + 1);
	int start = (int)floor(unit * mynum);
	int stop = (int)ceil(unit * (mynum + 1)) +
		(mynum != nprocs ? 1 : 0);
	printf(
		"Processing lines %d -> %d (DIMY = %d, nprocs = %d, unit = %g)\n",
		start, stop, DIMY, nprocs, unit);
	for (i = start; i < stop - 1; i++) { /* itt vegezzuk el a konvoluciot */
		for (j = 1; j < DIMX; j++) { /* a szeleket beken hagyjuk */
			oup[i][j] = 0;
			for (k = 0; k < 3; k++)
				for (l = 0; l < 3; l++)
					oup[i][j] += CM[k][l] * inp[i - k + 1][j - l + 1];
		}
	}

	/* Eredmenyek feldolgozasa */
	if (mynum == 0) { /* ha ez az elso peldany */
		int i;
		int linesize = sizeof(char) * DIMX; /* size of a line in bytes */
		int copylen = linesize * (int)floor(unit);
		char tmp[DIMX * DIMY], final[DIMX * DIMY];
		int of;
		printf("Elso peldany feldolgozasa\n");
		printf("Final image is %d bytes (sizeof = %d)\n", DIMX * DIMY, sizeof(final));
		for (i = 0; i <= nprocs; i++) {
			char fn[256];
			if (i != 0) {
				info = pvm_recv(-1, TAG_SUM);
				printf("Unpacking image from %d\n", i);
				info = pvm_upkbyte(tmp, DIMX * DIMY, 1);
				printf("Got image back from %d\n", i);
			}
			sprintf(fn, "out.%d", i);
			of = open(fn, O_WRONLY | O_CREAT, 0666);
			if (write(of, tmp, DIMX * DIMY) < DIMX * DIMY) { /* kiirjuk az eredmenyt */
				fprintf(stderr, "Iras\n");
				exit(1);
			}
			close(of);
			int offset = (int)(floor(unit * i)) * linesize; /* offset in bytes */
			printf("Copying %d bytes from offset %d to final image\n", copylen, offset);
			fflush(stdout);
			memcpy(final + offset, tmp + offset, copylen);
		}
		of = open("out.final", O_WRONLY | O_CREAT, 0666);
		if (write(of, final, DIMX * DIMY) < DIMX * DIMY) { /* kiirjuk az eredmenyt */
			fprintf(stderr, "Iras-final\n");
			exit(1);
		}
		close(of);
		fflush(stdout);
	} else { /* tovabbi peldanyok */
		/* A tobbi peldany elkuldi az eredmenyet, majd tovabbi feladatra var */
		info = pvm_initsend(PvmDataDefault);
		info = pvm_pkbyte(oup, DIMX*DIMY, 1);
		if (info < 0) {
			printf("a %d. peldanynal sikertelen pkbyte\n", mynum);
			exit(1);
		}
		info = pvm_send(tids[0], TAG_SUM);
		if (info < 0) {
			printf("a %d. peldanynal sikertelen send\n", i);
			exit(2);
		}
		printf("A %d. peldany elkuldte a kepet\n", mynum);
		fflush(stdout);
	}
	printf("A %d. peldany kilep a virtualis gepbol\n", mynum);
	pvm_exit();
	exit(0);
}

/*
 * SPMD modell miatt minden peldany vegrehajtja.
 * Az elso peldany belep a PVM rendszerbe es elinditja 
 * sajat magat nproc peldanyban. 
 */
void startup(int *pmynum, int *pnprocs, int tids[])
{
	int i, mynum, nprocs, info, mytid, numt, parent_tid;

	mytid = pvm_mytid();
	if (mytid < 0) {
		printf("sikertelen mytid\n");
		exit(0);
	}
	parent_tid = pvm_parent();
	if (parent_tid == PvmNoParent) {
		mynum = 0;
		tids[0] = mytid;
		printf("Hany node peldany (1 - %d)?\n", MAXPROCS);
		scanf("%d", &nprocs);
		if (nprocs > MAXPROCS) {
			printf("Tul sok node peldany! Porbalja ujra!\n");
			exit(0);
		}
		if (nprocs < 0) {
			printf("Kevesebb mint 0 peldany ? Probalja ujra!\n");
			exit(0);
		}
		numt = pvm_spawn("filter", NULL, PvmTaskDefault, "", nprocs, &tids[1]);
		if (numt != nprocs) {
			printf("Hibas taszk inditas numt = %d\n", numt);
			exit(0);
		}
		printf("%d peldany elindult\n", numt);
		for (i = 0; i <= nprocs; i++)
			printf("task %d tid = %d \n", i, tids[i]);
		/************************** <filt.c> *********************************/
		int inf;
		register i, j, k, l;

		if (_argc < 2) {
			fprintf(stderr, "Filenev????\n");
			exit(1);
		}

		inf = open(_argv[1], O_RDONLY);

		if (inf < 0) {
			fprintf(stderr, "File i/o\n");
			exit(1);
		}
		if (read(inf, inp, DIMX * DIMY) < DIMX * DIMY) {
			fprintf(stderr, "Keves\n");
			exit(1);
		}
		/************************** </filt.c> *********************************/
		info = pvm_initsend(PvmDataDefault); 	/* tid info az mindenkinek */
		info = pvm_pkint(&nprocs, 1, 1);
		info = pvm_pkint(tids, nprocs + 1, 1);
		info = pvm_pkbyte(inp, DIMX * DIMY, 1);
		info = pvm_mcast(&tids[1], nprocs, TAG_TIDS);
		printf("Sent image\n");
	} else {
		/* tid info vetele, es a sajat taszk sorszam meghatarozasa (mynum) */
		info = pvm_recv(parent_tid, TAG_TIDS);
		info = pvm_upkint(&nprocs, 1, 1);
		info = pvm_upkint(tids, nprocs + 1, 1);
		info = pvm_upkbyte(inp, DIMX * DIMY, 1);
		printf("Got image\n");
		for (i = 1; i <= nprocs; i++)
			if (mytid == tids[i]) mynum = i;
	}
	*pmynum = mynum;
	*pnprocs = nprocs; /* node peldanyok szama */
}
