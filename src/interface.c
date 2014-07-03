/*
 
 Copyright (C) 2013 Timo Lassmann <timolassmann@gmail.com>
 
 This file is part of TagDust.
 
 TagDust is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 TagDust is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with Tagdust.  If not, see <http://www.gnu.org/licenses/>.
 
 */


/*! \file interface.c
 \brief Functions to deal with user inputs.
 */

#include "interface.h"

#include "misc.h"

#ifndef MMALLOC
#include "malloc_macro.h"
#endif

#if HAVE_CONFIG_H
#include "config.h"
#endif


/** \fn struct parameters* interface(struct parameters* param,int argc, char *argv[])
 \brief Read command line options into @ref parameters.
 
 \param param nucleotide sequence.
 \param argc number of command line arguments.
  \param argv command line arguments.
 */
struct parameters* interface(struct parameters* param,int argc, char *argv[])
{
	int i,j,c;
	int help = 0;
	int version = 0;
	
	if (argc < 2 && isatty(0)){
		usage();
		exit(EXIT_SUCCESS);
	}
		
	MMALLOC(param,sizeof(struct parameters));
	param->infiles = 0;
	param->infile = 0;
	param->outfile = 0;
	
	param->quiet_flag = 0;
	param->num_query = 1000000;
	param->format = 0;
	param->gzipped = 0;
	param->bzipped = 0;
	param->sam = 0;
	param->buffer = 0;
		
	
	while (1){	 
		static struct option long_options[] ={
			{"help",0,0,'h'},
			{"version",0,0,'v'},
			{"log",required_argument,0,'l'},
			{0, 0, 0, 0}
		};
		
		int option_index = 0;
		c = getopt_long_only (argc, argv,"Q:e:o:p:q:hvf:t:i:l:L:a:",long_options, &option_index);
		
		if (c == -1){
			break;
		}
		
		switch(c) {
			case 0:
				break;
			
		
			case 'o':
				param->outfile = optarg;
				break;
			case 'h':
				help = 1;
				break;
			case 'v':
				version = 1;
				break;
			case '?':
				exit(1);
				break;
			default:
				fprintf(stderr,"default\n\n\n\n");
				abort ();
		}
	}
	if(help){
		usage();
		free_param(param);
		exit(EXIT_SUCCESS);
	}
	
	if(version){
		fprintf(stdout,"%s %s\n",PACKAGE_NAME,PACKAGE_VERSION);
		free_param(param);
		exit(EXIT_SUCCESS);
	}
	
	MMALLOC(param->buffer,sizeof(char) * MAX_LINE);
	
	
	sprintf(param->buffer , "%s %s, Copyright (C) 2013 Timo Lassmann <%s>\n",PACKAGE_NAME, PACKAGE_VERSION,PACKAGE_BUGREPORT);
	param->messages = append_message(param->messages, param->buffer  );
	//command_line[0] = 0;
	//c = 0;
	param->buffer[0] = 'c';
	param->buffer[1] = 'm';
	param->buffer[2] = 'd';
	param->buffer[3] = ':';
	param->buffer[4] = ' ';
	c = 5;
	for(i =0 ; i < argc;i++){
		for(j = 0; j < strlen(argv[i]);j++){
			param->buffer[c] = argv[i][j];
			c++;
		}
		param->buffer[c] = ' ';
		c++;
		
	}
	param->buffer[c] = '\n';
	param->buffer[c+1] = 0;
	
	param->messages = append_message(param->messages, param->buffer );
	
	
	//if(param->matchstart)
	//fprintf(stderr,"Viterbi: %d\n",param->viterbi);
	
	if(param->outfile == 0){
		sprintf(param->buffer , "ERROR: You need to specify an output file prefix using the -o / -out option.\n");
		param->messages = append_message(param->messages, param->buffer  );
		sprintf(param->buffer , "\tfor additional help: tagdust -help\n");
		param->messages = append_message(param->messages, param->buffer  );
		
		free_param(param);
		exit(EXIT_FAILURE);
		
	}	
	
	
	
		
	MMALLOC(param->infile,sizeof(char*)* (argc-optind));
	
	c = 0;
	while (optind < argc){
		param->infile[c] =  argv[optind++];
		c++;
	}
	param->infiles = c;
	return param;
}


/** \fn void usage()
 \brief Prints usage.
 */

void usage()
{
	fprintf(stdout, "\n%s %s, Copyright (C) 2013 Timo Lassmann <%s>\n",PACKAGE_NAME, PACKAGE_VERSION,PACKAGE_BUGREPORT);
	fprintf(stdout, "\n");
	fprintf(stdout, "Usage:   simreads  [options] <barcodefile from EDITTAG>-o <file>  .... \n\n");
	fprintf(stdout, "Options:\n");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_barlen","INT","", "Barcode length.");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_barnum","INT" ,"","Number of samples.");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_5seq","STR" ,"", "Sequence of 5' linker.");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_3seq","STR" ,"", "Sequence of 3' linker.");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_readlen","INT" ,"", "Length of read.");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_readlen_mod","INT" ,"", "+/- mod of read length.");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_error_rate","FLT" ,"", "Simulated error rate.");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_InDel_frac","FLT" ,"", "INDEL fraction.");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_numseq","INT" ,"", "Number of simulated sequences.");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_random_frac","FLT" ,"", "Fraction of totally random sequences.");
	fprintf (stdout,"\t%-17s%10s%7s%-30s\n","-sim_endloss","INT" ,"", "mean number of nucleotides lost on either end of the read.");

	
	fprintf(stdout, "\n");
	
}


void free_param(struct parameters* param)
{
	char logfile[200];
	FILE* outfile = 0;
	//if(param->log){
	if(param->outfile){
		sprintf (logfile, "%s_logfile.txt",param->outfile);
		if ((outfile = fopen( logfile, "w")) == NULL){
			fprintf(stderr,"can't open logfile\n");
			exit(-1);
		}
		fprintf(outfile,"%s\n",param->messages);
	
		fclose(outfile);
	
	}
	
	
	MFREE (param->infile);
	MFREE(param->messages);
	MFREE(param->buffer);
	MFREE(param);
}



