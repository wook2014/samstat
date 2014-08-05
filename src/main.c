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

#ifndef MMALLOC
#include "malloc_macro.h"
#endif

#include "interface.h"
#include "nuc_code.h"
#include "misc.h"
#include "io.h"
#include "hmm.h"
#include "viz.h"

#include <ctype.h>
#include <time.h>
#include <sys/stat.h>


#define MAX_SEQ_LEN 1000
#define MAXERROR 100

struct seq_stats{
	int** seq_len;
	int*** nuc_composition;
	int** seq_quality;
	int* aln_quality;
	int* alignments;
	int* nuc_num;
	float** errors;
	double* percent_identity;
	int*** mismatches;
	int*** insertions;
	int** deletions;
	int* base_qualities;
	int base_quality_offset;
	int sam;
	int md;
	int min_len;
	int max_len;
	int average_len;
	int max_error_per_read;
	
};

struct hmm* init_samstat_hmm(int average_length);
struct seq_stats* init_seq_stats(void);
struct seq_stats* reformat_base_qualities(struct seq_stats* seq_stats);

void free_seq_stats(struct seq_stats* seq_stats);
void print_stats(struct seq_stats* seq_stats);
int parse_cigar_md(struct read_info* ri,struct seq_stats* seq_stats,int qual_key);

char* make_file_stats(char* filename,char* buffer);

int main (int argc,char * argv[]) {
	struct parameters* param = 0;
	struct seq_stats* seq_stats = 0;
	
	struct hmm_data* hmm_data= 0;
	struct hmm* hmm = 0;
	
	struct hmm* background_hmm = 0;
	
	int (*fp)(struct read_info** ,struct parameters*,FILE* ) = 0;
	FILE* file = 0;
	int numseq = 0;
	int i,j,c;
	
	
	init_nuc_code();
	
	param = interface(param,argc,argv);
	
	sprintf(param->buffer,"Start Run\n--------------------------------------------------\n");
	param->messages = append_message(param->messages, param->buffer);
	
	struct read_info** ri = 0;
#ifdef DEBUG
	param->num_query = 1000;
#else
	param->num_query = 1000000;
#endif
	ri = malloc_read_info(ri, param->num_query );
	
	
	
	MMALLOC(hmm_data, sizeof(struct hmm_data));
	hmm_data->length = 0;
	hmm_data->score = 0;
	hmm_data->string = 0;
	hmm_data->iterations = 5;
	hmm_data->run_mode = MODE_BAUM_WELCH;
	hmm_data->num_threads = 4;
	hmm_data->weight = 0;
	
	MMALLOC(hmm_data->length,sizeof(int) *param->num_query);
	MMALLOC(hmm_data->weight,sizeof(float) *param->num_query);
	MMALLOC(hmm_data->score,sizeof(float) *param->num_query);
	MMALLOC(hmm_data->string , sizeof(char* ) * param->num_query);
	for(i = 0; i < param->num_query;i++){
		hmm_data->length[i] = 0;
		hmm_data->score[i] = prob2scaledprob(0.0);
		hmm_data->weight[i] = prob2scaledprob(1.0);
		hmm_data->string[i] = 0;
	}
	
	file =  io_handler(file, 0,param);
	if(param->sam == 0){
		fp = &read_fasta_fastq;
	}else {
		fp = &read_sam_chunk;
	}
	
	seq_stats = init_seq_stats();
	seq_stats->sam = param->sam;
	int qual_key = 0;
	int aln_len = 0;
	int first_lot =1;
	int use_mapping_qualities_for_hmm_training = 0;
	
	while ((numseq = fp(ri, param,file)) != 0){
		for(i = 0; i < numseq;i++){
			if(ri[i]->len > seq_stats->max_len){
				seq_stats->max_len = ri[i]->len;
			}
			
			if(ri[i]->len < seq_stats->min_len ){
				seq_stats->min_len = ri[i]->len;
			}
			seq_stats->average_len += ri[i]->len;
			
			if(ri[i]->mapq < 0){
				qual_key = 5;
			}else if(ri[i]->mapq < 3){
				qual_key = 0;
			}else if(ri[i]->mapq < 10){
				qual_key = 1;
			}else if(ri[i]->mapq < 20){
				qual_key = 2;
			}else if(ri[i]->mapq < 30){
				qual_key = 3;
			}else{
				qual_key = 4;
			}
			
			if(ri[i]->cigar && ri[i]->md){
				if(ri[i]->cigar[0] != '*'){
					aln_len = parse_cigar_md(ri[i],seq_stats, qual_key);
					seq_stats->md = 1;
				}
			}
			
			if(ri[i]->strand != 0){
				ri[i]->seq = reverse_complement(ri[i]->seq,ri[i]->len);
				if(ri[i]->qual[0] != '*'){
					reverse_sequence(ri[i]->qual, ri[i]->len);
				}
			}
			
			if(ri[i]->qual){
				if(ri[i]->qual[0] != '*'){
					for(j = 0; j < ri[i]->len;j++){
						seq_stats->seq_quality[qual_key][j] += (int)(ri[i]->qual[j]);
						seq_stats->base_qualities[(int)(ri[i]->qual[j])]++;
					}
				}
			}
			
			seq_stats->alignments[qual_key]++;
			
			// sequence length
			if(ri[i]->len >=  MAX_SEQ_LEN){
				seq_stats->seq_len[qual_key][MAX_SEQ_LEN-1]++;
			}else{
				seq_stats->seq_len[qual_key][ri[i]->len]++;
			}
			// sequence composition
			for(j = 0;j <  ri[i]->len;j++){
				seq_stats->nuc_num[(int)ri[i]->seq[j]]++;
				seq_stats->nuc_composition[qual_key][j][(int)ri[i]->seq[j]]++;
			}
			
			
			if(ri[i]->errors != -1){
				
				
				if(ri[i]->errors > seq_stats->max_error_per_read){
					seq_stats->max_error_per_read = ri[i]->errors;
				}
				
				seq_stats->percent_identity[qual_key] +=(((double)aln_len - (double)ri[i]->errors) / (double)aln_len * 100.0);// ((float)aln_len - (float)ri[i]->errors) / (float) aln_len * 100.0f;
				
				//if((int)  floor((((float)aln_len - (float)ri[i]->errors) / (float)aln_len * 1000.0f) + 0.5f ) > 1000 || (int)  floor((((float)aln_len - (float)ri[i]->errors) / (float)aln_len * 1000.0f) + 0.5f ) < 0 ){
				//	fprintf(stderr,"ERROR: %f	%f\n", (float)aln_len,  (float)ri[i]->errors);
				//}
				
				//fprintf(stderr,"%d	%d	%d	%d\n",qual_key,aln_len,ri[i]->errors,(int)  floor((((float)aln_len - (float)ri[i]->errors) / (float)aln_len * 1000.0f) + 0.5 ));
				if(ri[i]->errors >= MAXERROR){
					seq_stats->errors[qual_key][MAXERROR-1]++;
				}else{
					seq_stats->errors[qual_key][ri[i]->errors]++;
				}
			}
			
			
		}
		//needs to be run after sequences are reverse complemented.....
		if(first_lot){
			
			seq_stats->average_len = (int) floor((double) seq_stats->average_len / (double) numseq   + 0.5);
			//seq_stats = reformat_base_qualities(seq_stats);
			hmm = init_samstat_hmm(seq_stats->average_len);
			for(i = 0; i < numseq;i++){
				if(ri[i]->mapq != -1){
					use_mapping_qualities_for_hmm_training++;
				}
				// set mapping quality of 0 to a small number
				if(ri[i]->mapq == 0){
					ri[i]->mapq = 0.00001;
				}
			}
			
			//all sequences have a mapping quality.
			if(use_mapping_qualities_for_hmm_training == numseq){
				use_mapping_qualities_for_hmm_training = 1;
			}else{
				use_mapping_qualities_for_hmm_training = 0;
			}
			
			for(i = 0; i < numseq;i++){
				hmm_data->length[i] = ri[i]->len;
				hmm_data->string[i] = ri[i]->seq;

				if(use_mapping_qualities_for_hmm_training){
					hmm_data->weight[i] = prob2scaledprob(1.0 - pow(10.0, -1.0 *  ri[i]->mapq/ 10.0));
				}else{
					hmm_data->weight[i] = prob2scaledprob(1.0);
				}
			//	fprintf(stderr, "%d: %f %f %f\t->\t%f\n",i, hmm_data->weight[i] , scaledprob2prob(hmm_data->weight[i] ),ri[i]->mapq ,     1.0 - pow(10.0, -1.0 *  ri[i]->mapq/ 10.0) );
				//fprintf(stderr,"%f	->%f\n",ri[i]->mapq ,     1.0 - pow(10.0, -1.0 *  ri[i]->mapq/ 10.0));
			}
			hmm_data->num_seq =numseq;
			hmm = run_EM_iterations(hmm,hmm_data);
			fprintf(stderr,"FORGROUND:\n");
			print_hmm_parameters(hmm);
			if(use_mapping_qualities_for_hmm_training){
				background_hmm =init_samstat_hmm(seq_stats->average_len);
				for(i = 0; i < numseq;i++){
					hmm_data->length[i] = ri[i]->len;
					hmm_data->string[i] = ri[i]->seq;
					hmm_data->weight[i] =  prob2scaledprob(1.0 -(1.0 - pow(10.0, -1.0 *  ri[i]->mapq/ 10.0)) );
					//fprintf(stderr,"%f	->%f\n",ri[i]->mapq ,     1.0 - pow(10.0, -1.0 *  ri[i]->mapq/ 10.0));
					//	fprintf(stderr, "%d: %f %f %f\t->\t%f\n",i, hmm_data->weight[i] , scaledprob2prob(hmm_data->weight[i] ),ri[i]->mapq ,     1.0 - pow(10.0, -1.0 *  ri[i]->mapq/ 10.0) );
				}
				hmm_data->num_seq =numseq;
				background_hmm = run_EM_iterations(background_hmm,hmm_data);
			}
			first_lot = 0;
			fprintf(stderr,"Background:\n");

			print_hmm_parameters(background_hmm);
			
		}
	}
	pclose(file);
	
	
	print_stats(seq_stats);

	struct plot_data* pd = 0;
	pd = malloc_plot_data(10, 255);
	
	
	sprintf(pd->plot_title, "%s",shorten_pathname(param->infile[0]));
	pd->description = make_file_stats(param->infile[0],pd->description);
	print_html5_header(stdout,pd);
	
	sprintf(pd->labels[0], "%s","Number");
	
	
	pd->data[5][0] =  seq_stats->alignments[5];
	sprintf(pd->series_labels[5], "Unmapped");
	
	pd->data[4][0] =  seq_stats->alignments[0];
	sprintf(pd->series_labels[4], "MAPQ < 3");
	pd->data[3][0] =  seq_stats->alignments[1];
	
	sprintf(pd->series_labels[3], "MAPQ < 10");
	pd->data[2][0] =  seq_stats->alignments[2];
	
	sprintf(pd->series_labels[2], "MAPQ < 20");
	pd->data[1][0] =  seq_stats->alignments[3];
	
	sprintf(pd->series_labels[1], "MAPQ < 30");
	pd->data[0][0] =  seq_stats->alignments[4];
	
	sprintf(pd->series_labels[0], "MAPQ >= 30");
	
	sprintf(pd->description,"Number of alignments in various mapping quality (MAPQ) intervals and number of unmapped sequences.");
	
	pd->num_points = 1;
	pd->num_series = 6;
	sprintf(pd->plot_title, "Mapping stats:");
	pd->plot_type = PIE_PLOT;
	print_html5_chart(stdout, pd);
	print_html_table(stdout, pd);

        
        
        for(i = 0; i < 6;i++){
                switch (i) {
                        case 0:
                                sprintf(pd->plot_title, "Read Length Distribution(MAPQ >= 30):");
                                sprintf(pd->description,"Length Distribution of MAPQ >= 30 reads.");
                                break;
                        case 1:
                                sprintf(pd->plot_title, "Read Length Distribution(MAPQ < 30):");
                                sprintf(pd->description,"Length Distribution of MAPQ < 30 reads.");
                                break;

                        case 2:
                                sprintf(pd->plot_title, "Read Length Distribution(MAPQ < 20):");
                                sprintf(pd->description,"Length Distribution of MAPQ < 20 reads.");
                                break;

                        case 3:
                                sprintf(pd->plot_title, "Read Length Distribution(MAPQ < 10):");
                                sprintf(pd->description,"Length Distribution of MAPQ < 10 reads.");
                                break;

                        case 4:
                                sprintf(pd->plot_title, "Read Length Distribution(MAPQ < 3):");
                                sprintf(pd->description,"Length Distribution of MAPQ < 3 reads.");
                                break;
                        case 5:
                                sprintf(pd->plot_title, "Read Length Distribution(unmapped):");
                                sprintf(pd->description,"Length Distribution of unmapped reads.");
                                break;


                                
                        default:
                                break;
                }
                if(seq_stats->alignments[i]){
                        for(j = seq_stats->min_len; j <= seq_stats->max_len;j++){
                                sprintf(pd->labels[j-seq_stats->min_len], "%d",j);
                                pd->data[0][j-seq_stats->min_len] = seq_stats->seq_len[i][j];
                        }
                        pd->num_points = seq_stats->max_len - seq_stats->min_len;
                        pd->num_series = 1;
                        
                        
                        pd->plot_type = BAR_PLOT;
                        print_html5_chart(stdout, pd);
                }
        }
	
	fprintf(stderr,"Errors :\n");
	for(i = 0; i < 5;i++){
                switch (i) {
                        case 0:
                                sprintf(pd->plot_title, "Number of Errors Per Read (MAPQ >= 30):");
                                sprintf(pd->description,"Barplot shows the percentage of reads (y-axis) with 0, 1, 2 ... errors (x axis) for MAPQ >= 30 reads.");
                                break;
                        case 1:
                                sprintf(pd->plot_title, "Number of Errors Per Read(MAPQ < 30):");
                                sprintf(pd->description,"Barplot shows the percentage of reads (y-axis) with 0, 1, 2 ... errors (x axis) for MAPQ < 30 reads.");
                                break;
				
                        case 2:
                                sprintf(pd->plot_title, "Number of Errors Per Read(MAPQ < 20):");
                                sprintf(pd->description,"Barplot shows the percentage of reads (y-axis) with 0, 1, 2 ... errors (x axis) for MAPQ < 20 reads.");
                                break;
				
                        case 3:
                                sprintf(pd->plot_title, "Number of Errors Per Read(MAPQ < 10):");
                                sprintf(pd->description,"Barplot shows the percentage of reads (y-axis) with 0, 1, 2 ... errors (x axis) for MAPQ < 10 reads.");
                                break;
				
                        case 4:
                                sprintf(pd->plot_title, "Number of Errors Per Read(MAPQ < 3):");
                                sprintf(pd->description,"Barplot shows the percentage of reads (y-axis) with 0m, 1, 2 ... errors (x axis) for MAPQ < 3 reads.");
                                break;
                       
                                
                        default:
                                break;
                }
                if(seq_stats->alignments[i]){
                        for(j = 0; j <= seq_stats->max_error_per_read;j++){
                                sprintf(pd->labels[j], "%d",j);
                                pd->data[0][j] = 100.0 * (float)seq_stats->errors[i][j]/ (float)seq_stats->alignments[i];
                        }
                        pd->num_points = seq_stats->max_error_per_read ;
                        pd->num_series = 1;
                        
                        
                        pd->plot_type = BAR_PLOT;
                        print_html5_chart(stdout, pd);
                }
        }
        
	sprintf(pd->series_labels[0],"A");
        sprintf(pd->series_labels[1],"C");
        sprintf(pd->series_labels[2],"G");
        sprintf(pd->series_labels[3],"T");
        sprintf(pd->series_labels[4],"N");
	
	sprintf(pd->plot_title, "HMM stuff..,");
	sprintf(pd->description,"Distribution of Mismatches in MAPQ >= 30 reads.<p style=\"font-weight: bold;font-size: 200%%\"> Legend: <span style=\"color: %s;\">A</span><span style=\"color: %s;\">C</span><span style=\"color: %s;\">G</span><span style=\"color: %s;\">T</span></p> ",colors[0],colors[1],colors[2],colors[3] );
	for(j = 2; j <= 3+seq_stats->average_len -1;j++){
		sprintf(pd->labels[j-2], "%d",j-1);
		for(c = 0; c < 5;c++){
			pd->data[c][j-2] =  log2f(scaledprob2prob(hmm->emissions[j][c] ) /scaledprob2prob(background_hmm->emissions[j][c]));
 		}
	}

	pd->num_points = seq_stats->average_len+1;
	pd->num_series = 4;
	pd->plot_type = LINE_PLOT;
	print_html5_chart(stdout, pd);





        sprintf(pd->series_labels[0],"A");
        sprintf(pd->series_labels[1],"C");
        sprintf(pd->series_labels[2],"G");
        sprintf(pd->series_labels[3],"T");
        sprintf(pd->series_labels[4],"N");
        
        
        for(i = 0; i < 5;i++){
                switch (i) {
                        case 0:
                                sprintf(pd->plot_title, "Distribution of Mismatches (MAPQ >= 30):");
                                sprintf(pd->description,"Distribution of Mismatches in MAPQ >= 30 reads.<p style=\"font-weight: bold;font-size: 200%%\"> Legend: <span style=\"color: %s;\">A</span><span style=\"color: %s;\">C</span><span style=\"color: %s;\">G</span><span style=\"color: %s;\">T</span></p> ",colors[0],colors[1],colors[2],colors[3] );
                                break;
                        case 1:
                                sprintf(pd->plot_title, "Distribution of Mismatches (MAPQ < 30):");
                                sprintf(pd->description,"Distribution of Mismatches in MAPQ < 30 reads.<p style=\"font-weight: bold;font-size: 200%%\"> Legend: <span style=\"color: %s;\">A</span><span style=\"color: %s;\">C</span><span style=\"color: %s;\">G</span><span style=\"color: %s;\">T</span></p> ",colors[0],colors[1],colors[2],colors[3] );
                                break;
                                
                        case 2:
                                sprintf(pd->plot_title, "Distribution of Mismatches (MAPQ < 20):");
                                sprintf(pd->description,"Distribution of Mismatches in MAPQ < 20 reads.<p style=\"font-weight: bold;font-size: 200%%\"> Legend: <span style=\"color: %s;\">A</span><span style=\"color: %s;\">C</span><span style=\"color: %s;\">G</span><span style=\"color: %s;\">T</span></p> ",colors[0],colors[1],colors[2],colors[3] );
                                break;
                                
                        case 3:
                                sprintf(pd->plot_title, "Distribution of Mismatches (MAPQ < 10):");
                                sprintf(pd->description,"Distribution of Mismatches in MAPQ < 10 reads.<p style=\"font-weight: bold;font-size: 200%%\"> Legend: <span style=\"color: %s;\">A</span><span style=\"color: %s;\">C</span><span style=\"color: %s;\">G</span><span style=\"color: %s;\">T</span></p> ",colors[0],colors[1],colors[2],colors[3] );
                                break;
                                
                        case 4:
                                sprintf(pd->plot_title, "Distribution of Mismatches(MAPQ < 3):");
                                sprintf(pd->description,"Distribution of Mismatches in MAPQ < 3 reads.<p style=\"font-weight: bold;font-size: 200%%\"> Legend: <span style=\"color: %s;\">A</span><span style=\"color: %s;\">C</span><span style=\"color: %s;\">G</span><span style=\"color: %s;\">T</span></p> ",colors[0],colors[1],colors[2],colors[3] );
                                break;
                                
                        default:
                                break;
                }
		if(seq_stats->alignments[i]){
			for(j = 0; j <= seq_stats->max_len;j++){
                                sprintf(pd->labels[j], "%d",j+1);
				for(c = 0; c < 5;c++){
					pd->data[c][j-seq_stats->min_len] =  (float)seq_stats->mismatches[i][j][c] / (float)seq_stats->alignments[i];
				}
			}
		}
                pd->num_points = seq_stats->max_len;
                pd->num_series = 4;
                pd->plot_type = BAR_PLOT;
                print_html5_chart(stdout, pd);
	}
        
	print_html5_footer(stdout);
	
	free_plot_data(pd);
	
	free_seq_stats(seq_stats);
	free_param(param);
	return EXIT_SUCCESS;
	
}

char* make_file_stats(char* filename,char* buffer)
{
	struct stat buf;
	int local_ret;
	
	char time_string[200];
	int hour;
	char am_or_pm;
	struct tm *ptr;
		
	local_ret= stat ( filename, &buf );
	if ( local_ret == 0 ){
		// %9jd", (intmax_t)statbuf.st_size);
		
		ptr = localtime(&buf.st_mtime);
		hour = ptr->tm_hour;
		if (hour <= 11)
			am_or_pm = 'a';
		else {
			hour -= 12;
			am_or_pm = 'p';
		}
		if (hour == 0){
			hour = 12;
		}
		strftime(time_string, 200, "%F %H:%M:%S\t", ptr);

		sprintf(buffer,"size:%9jd  created: %s",(intmax_t)buf.st_size, time_string);
	}else{
		fprintf(stderr,"Failed getting stats for file:%s\n",filename );
	}
	
	
	return buffer;
}


struct seq_stats* reformat_base_qualities(struct seq_stats* seq_stats)
{
	//From wikipedia:
	/*SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS.....................................................
	..........................XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX......................
	...............................IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII......................
	.................................JJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJ......................
	LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL....................................................
	!"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~
	|                         |    |        |                              |                     |
	33                        59   64       73                            104                   126
	0........................26...31.......40
	-5....0........9.............................40
	0........9.............................40
	3.....9.............................40
	0.2......................26...31.........41
	
	S - Sanger        Phred+33,  raw reads typically (0, 40)
	X - Solexa        Solexa+64, raw reads typically (-5, 40)
	I - Illumina 1.3+ Phred+64,  raw reads typically (0, 40)
	J - Illumina 1.5+ Phred+64,  raw reads typically (3, 40)
	with 0=unused, 1=unused, 2=Read Segment Quality Control Indicator (bold)
	(Note: See discussion above).
	L - Illumina 1.8+ Phred+33,  raw reads typically (0, 41)*/
	int start = -1;
	int stop = -1;
	//fprintf(stderr,"Got here\n");
	int i;
	for(i =0;i < 256;i++){
		if(seq_stats->base_qualities[i]){
			if(start == -1){
				start = i;
			}
			stop = i;
		}
	}
#ifdef DEBUG
	if(start == 33 && stop == 73 ){
		fprintf(stderr,"S - Sanger\n");
	}
	if(start == 49 && stop == 104){
		fprintf(stderr,"X - SOLEXA\n");
	}
	if(start == 64 && stop == 104){
		fprintf(stderr,"Illumina 1.3+\n");
	}
	if(start == 66&& stop == 104 ){
		fprintf(stderr,"Illumina 1.5+\n");
	}
	if(start == 33 && stop == 74){
		fprintf(stderr,"Illumina 1.8+\n");
	}
#endif

	seq_stats->base_quality_offset = start;
	return seq_stats;
}


struct hmm* init_samstat_hmm(int average_length)
{
	struct hmm* hmm = malloc_hmm(average_length * 2 + 3, 5,100);
	
	init_logsum();
	
	int i,j,c;
	for(i = 0; i < hmm->num_states;i++){
		for(j = 0; j < hmm->num_states;j++){
			hmm->transitions[i][j] = prob2scaledprob(0.0);
			hmm->transitions_e[i][j] = prob2scaledprob(0.5);
		}
	}
	
	hmm->transitions[STARTSTATE ][2]= prob2scaledprob(0.5f);
	hmm->transitions[STARTSTATE][3] = prob2scaledprob(0.5f);
	
	hmm->transitions[2][2]= prob2scaledprob(0.5f);
	hmm->transitions[2][3]= prob2scaledprob(0.5f);

	
	
	
	for(i = 0; i < average_length ;i++ ){
		//match states
		hmm->transitions[3+i][3+i + average_length] =  prob2scaledprob(0.5);
		if(3+i +1 <average_length  + 3){
			hmm->transitions[3+i][3+i +1]=  prob2scaledprob(0.5);
		}
		if(3+i +2 < average_length  + 3){
			hmm->transitions[3+i][3+i + 2] =  prob2scaledprob(0.5);
		}
		if(3+i +3 < average_length + 3){
			hmm->transitions[3+i][3+i + 3] =  prob2scaledprob(0.5);
		}
		
		// insert states
		hmm->transitions[3+i + average_length][3+i + average_length] = prob2scaledprob(0.5f);
		//if(
		if(3+i +1 <average_length  + 3){
			hmm->transitions[3+i + average_length][3+i +1] = prob2scaledprob(0.5f);
		}
	}
	hmm->transitions[3+average_length-1][ENDSTATE] = prob2scaledprob(0.5);
	
	hmm->transitions[3+average_length+average_length-1][3+average_length+average_length-1] =prob2scaledprob (0.5);
	hmm->transitions[3+average_length+average_length-1][ENDSTATE] = prob2scaledprob(0.5);
	
	//norm;
	float sum = prob2scaledprob(0.0);
	for(i = 0; i < hmm->num_states;i++){
		if(i != 1){
			sum = prob2scaledprob(0.0);
			for(j = 0; j < hmm->num_states;j++){
				sum =  logsum(sum ,hmm->transitions[i][j] ) ;
				if(hmm->transitions[i][j] == -INFINITY){
					hmm->transitions_e[i][j] = prob2scaledprob(0.0f);
				}
			}
			
			for(j = 0; j < hmm->num_states;j++){
				hmm->transitions[i][j] = hmm->transitions[i][j] -sum;
			}
		}
	}
	
	
	
	for(i =1 ; i < hmm->num_states;i++){
		MMALLOC(hmm->emissions[i], sizeof(float) * hmm->alphabet_len);
		MMALLOC(hmm->emissions_e[i], sizeof(float) * hmm->alphabet_len);
		for(j = 0;j < hmm->alphabet_len;j++){
			hmm->emissions[i][j] = prob2scaledprob(1.0f / (float) hmm->alphabet_len);
			hmm->emissions_e[i][j] = prob2scaledprob(0.5);
		}
	}
	
	for(i = 0; i < hmm->num_states;i++){
		c = 0;
		for(j = 0; j < hmm->num_states;j++){
			if(hmm->transitions[i][j]  != -INFINITY){
				hmm->tindex[i][c+1] = j;
				c++;
			}
		}
		hmm->tindex[i][0] = c+1;
	}
	
	//print_hmm_parameters(hmm);
	//exit(-1);
	return hmm;

}

struct seq_stats* init_seq_stats(void)
{
	struct seq_stats* seq_stats = NULL;
	int i,j,c;
	
	MMALLOC(seq_stats, sizeof(struct seq_stats));
	
	seq_stats->alignments = NULL;
	seq_stats->aln_quality = NULL;
	seq_stats->deletions = NULL;
	seq_stats->errors = NULL;
	seq_stats->insertions =NULL;
	seq_stats->mismatches = NULL;
	seq_stats->nuc_composition = NULL;
	seq_stats->nuc_num = NULL;
	seq_stats->percent_identity = NULL;
	seq_stats->seq_len = NULL;
	seq_stats->seq_quality = NULL;
	seq_stats->base_qualities = NULL;
	
	MMALLOC(seq_stats->base_qualities, sizeof(int)* 256);
	MMALLOC(seq_stats->alignments, sizeof(int)* 6);
	
	MMALLOC(seq_stats->seq_len,sizeof(int*)* 6);
	MMALLOC(seq_stats->nuc_composition,sizeof(int**)* 6);
	MMALLOC(seq_stats->seq_quality,sizeof(int*)* 6);
	MMALLOC(seq_stats->aln_quality,sizeof(int)*6);
	MMALLOC(seq_stats->nuc_num,sizeof(int) * 6);
	//seq_stats->overall_kmers= malloc(sizeof(float) * KMERALLOC);
	MMALLOC(seq_stats->errors,sizeof(float*)* 6 );
	
	MMALLOC(seq_stats->percent_identity,sizeof(double)* 6 );
	
	MMALLOC(seq_stats->mismatches,sizeof(int**)* 6);
	MMALLOC(seq_stats->insertions, sizeof(int**) * 6);
	MMALLOC(seq_stats->deletions, sizeof(int*) * 6);
	
	for(i= 0; i < 256;i++){
		seq_stats->base_qualities[i] = 0;
	}
	
	for(c = 0; c < 6;c++){
		seq_stats->mismatches[c] = NULL;
		seq_stats->insertions[c] = NULL;
		seq_stats->deletions[c] = NULL;
		seq_stats->errors[c] = NULL;
		
		seq_stats->nuc_num[c] = 0;
		seq_stats->alignments[c] = 0;
		
		
		seq_stats->seq_len[c] = NULL;
		seq_stats->nuc_composition[c] = NULL;
		seq_stats->seq_quality[c] = NULL;
	
		
		
		
		MMALLOC(seq_stats->mismatches[c],sizeof(int*)* MAX_SEQ_LEN);
		MMALLOC(seq_stats->insertions[c],sizeof(int*) * MAX_SEQ_LEN);
		MMALLOC(seq_stats->deletions[c],sizeof(int) * MAX_SEQ_LEN);
		MMALLOC(seq_stats->errors[c],sizeof(float)* MAXERROR );
		
		MMALLOC(seq_stats->seq_len[c],sizeof(int)* MAX_SEQ_LEN);
		MMALLOC(seq_stats->nuc_composition[c],sizeof(int*)* MAX_SEQ_LEN);
		MMALLOC(seq_stats->seq_quality[c],sizeof(int)* MAX_SEQ_LEN);

		
		seq_stats->percent_identity[c] = 0.0f;
		        
		for(i = 0; i < MAXERROR;i++){
			seq_stats->errors[c][i] = 0;
			
		}
		
		for(i= 0; i < MAX_SEQ_LEN;i++){
			seq_stats->deletions[c][i] = 0;
			seq_stats->mismatches[c][i] = NULL;
			seq_stats->insertions[c][i] = NULL;
			seq_stats->seq_len[c][i] = 0;
			seq_stats->nuc_composition[c][i] = NULL;
			
			seq_stats->seq_quality[c][i] = 0;
			
			MMALLOC(seq_stats->mismatches[c][i],sizeof(int)*5);
			MMALLOC(seq_stats->insertions[c][i],sizeof(int)*5);
			MMALLOC(seq_stats->nuc_composition[c][i],sizeof(int*)* 5);
			for(j = 0; j < 5;j++){
				seq_stats->mismatches[c][i][j] = 0;
				seq_stats->insertions[c][i][j] = 0;
				seq_stats->nuc_composition[c][i][j] = 0;
			}
		}
	}
	
	seq_stats->sam = 0;
	seq_stats->md = 0;
	seq_stats->base_quality_offset = 33;
	seq_stats->min_len = 1000000000;
	seq_stats->max_len = -1000000000;
	seq_stats->max_error_per_read =-1000000000;
	return seq_stats;
}

void free_seq_stats(struct seq_stats* seq_stats)
{
	int i,j;
	
	for(j = 0; j < 6;j++){
		for(i= 0; i < MAX_SEQ_LEN;i++){
			free(seq_stats->mismatches[j][i]);// = malloc(sizeof(int)*5);
			free(seq_stats->insertions[j][i]);// = malloc(sizeof(int)*5);
		}
		free(seq_stats->mismatches[j]);// = malloc(sizeof(int*)* MAX_SEQ_LEN);
		free(seq_stats->insertions[j]);// = malloc(sizeof(int*) * MAX_SEQ_LEN);
		free(seq_stats->deletions[j]);// = malloc(sizeof(int) * MAX_SEQ_LEN);
	}
	free(seq_stats->mismatches);// = malloc(sizeof(int*)* MAX_SEQ_LEN);
	free(seq_stats->insertions);// = malloc(sizeof(int*) * MAX_SEQ_LEN);
	free(seq_stats->deletions);// = malloc(sizeof(int) * MAX_SEQ_LEN);
	
	for(i = 0; i < 6;i++){
		
		for(j = 0; j < MAX_SEQ_LEN;j++){
			free(seq_stats->nuc_composition[i][j]);// = malloc(sizeof(int*)* 5);
			//free(seq_stats->seq_quality[i][j]);// = malloc(sizeof(int)* 6);
			
		}
		//free(seq_stats->seq_quality[i]);
		free(seq_stats->seq_len[i]);// = malloc(sizeof(int)* MAX_SEQ_LEN);
		free(seq_stats->nuc_composition[i]);// = malloc(sizeof(int*)* MAX_SEQ_LEN);
		free(seq_stats->seq_quality[i]);// = malloc(sizeof(int*)* MAX_SEQ_LEN);
	}
	for(i = 0; i < 6;i++){
		free(seq_stats->errors[i]);
		//free(seq_stats->percent_identity[i]);
		
	}
	free(seq_stats->errors);
	free(seq_stats->percent_identity);
	free(seq_stats->base_qualities);
	free(seq_stats->alignments);
	free(seq_stats->nuc_num);
	free(seq_stats->seq_len);// = malloc(sizeof(int*)* 6);
	free(seq_stats->nuc_composition);// = malloc(sizeof(int**)* 6);
	free(seq_stats->seq_quality);// = malloc(sizeof(int**)* 6);
	free(seq_stats->aln_quality);// = malloc(sizeof(int)*6);
	free(seq_stats);// = malloc(sizeof(struct seq_stats));
	
}

 void print_stats(struct seq_stats* seq_stats)
{
	int i,j,c;
	
	fprintf(stderr,"Nucleotides:\n");
	for(c = 0; c < 6;c++){
		fprintf(stderr,"%d	%d\n",c,seq_stats->nuc_num[c]);
	}
	
	fprintf(stderr,"Alignments :\n");
	for(c = 0; c < 6;c++){
		fprintf(stderr,"%d	%d\n",c,seq_stats->alignments[c]);
	}
	
	fprintf(stderr,"Percentage Identitiy :\n");
	for(c = 0; c < 6;c++){
		fprintf(stderr,"%d	%f\n",c,seq_stats->percent_identity[c]);
	}
	
	fprintf(stderr,"Errors :\n");
	for(c = 0; c < 6;c++){
		if(seq_stats->alignments[c]){
		fprintf(stderr,"Class:%d\n",c);
		for(i = 0; i < MAXERROR;i++){
			fprintf(stderr," %f",seq_stats->errors[c][i]);
			
		}
		fprintf(stderr,"\n");
		}
	}
	
	fprintf(stderr,"Quality:  %d\n", seq_stats->base_quality_offset );
	for(c = 0; c < 6;c++){
		if(seq_stats->alignments[c]){
		fprintf(stderr,"Class:%d\n",c);
		for(i= 0; i <= seq_stats->max_len;i++){
			
			fprintf(stderr," %d",seq_stats->seq_quality[c][i] );
			
		}
		fprintf(stderr,"\n");
		}
	}
	
	fprintf(stderr,"Length :\n");
	for(c = 0; c < 6;c++){
		if(seq_stats->alignments[c]){
		fprintf(stderr,"Class:%d\n",c);
		for(i= 0; i <= seq_stats->max_len;i++){
			
			fprintf(stderr," %d",seq_stats->seq_len[c][i] );
			
		}
		fprintf(stderr,"\n");
		}
	}

	
	fprintf(stderr,"Deletions :\n");
	for(c = 0; c < 6;c++){
		if(seq_stats->alignments[c]){
		fprintf(stderr,"Class:%d\n",c);
		for(i= 0; i <= seq_stats->max_len;i++){
		
			fprintf(stderr," %d",seq_stats->deletions[c][i] );
			
		}
		fprintf(stderr,"\n");
		}
	}
	
	
	
	
	
	fprintf(stderr,"Mismatches / Insertions / composition  :\n");
	
	for(c = 0; c < 6;c++){
		if(seq_stats->alignments[c]){
		fprintf(stderr,"Class:%d\n",c);
		
		for(i= 0; i <= seq_stats->max_len;i++){
			fprintf(stderr,"Pos:%d ",i);
			for(j = 0; j < 5;j++){
				fprintf(stderr," %d",seq_stats->mismatches[c][i][j]);
			}
			for(j = 0; j < 5;j++){
				fprintf(stderr," %d",seq_stats->insertions[c][i][j]);
			}
			for(j = 0; j < 5;j++){
				
				fprintf(stderr," %d",seq_stats->nuc_composition[c][i][j]);
				
			}
			fprintf(stderr,"\n");
		
		}
		}
		
	}
}




int parse_cigar_md(struct read_info* ri,struct seq_stats* seq_stats,int qual_key)
	{
		int* read = malloc(sizeof(int)* MAX_SEQ_LEN);
		int* genome = malloc(sizeof(int) * MAX_SEQ_LEN);
		int reverse_int[5]  ={3,2,1,0,4};
		char tmp_num[8];
		int l,i,j,c,rp,gp,sp,exit_loop,aln_len,add;
		
		for(i = 0; i < MAX_SEQ_LEN;i++){
			genome[i] = 0;
			read[i] = 0;
		}
		
		l = (int) strlen((char*)ri->cigar);
		exit_loop = 0;
		i =0;
		rp = 0;
		sp = 0;
		while(!exit_loop){
			c = 0;
			if(isdigit((int)ri->cigar[i])){
				j = 0;
				while (isdigit(ri->cigar[i])) {
					tmp_num[j] = ri->cigar[i];
					j++;
					i++;
					if(i == l){
						exit_loop =1;
						break;
					}
				}
				tmp_num[j] = 0;
				
				c = atoi(tmp_num);
			}
			if(isalpha((int)ri->cigar[i])){
				switch (ri->cigar[i]) {
					case 'M':
						for(j = 0; j < c;j++){
							read[rp] = ri->seq[sp];
							rp++;
							sp++;
						}
						//			fprintf(stderr,"M:%d\n",c);
						break;
					case 'I':
						for(j = 0; j < c;j++){
							read[rp] = ri->seq[sp];
							genome[rp] = -1;
							rp++;
							sp++;
						}
						
						//			fprintf(stderr,"I:%d\n",c);
						break;
					case 'D':
						for(j = 0; j < c;j++){
							read[rp] = -1;
							rp++;
							
						}
						
						//			fprintf(stderr,"D:%d\n",c);
						break;
					default:
						break;
				}
				c = 0;
			}
			i++;
			if(i == l){
				exit_loop =1;
				break;
			}
			
		}
		aln_len = rp;
		
		i =0;
		rp = 0;
		
		while(read[rp] == -1){
			rp++;
		}
		gp = 0;
		exit_loop = 0;
		add  = 0;
		l = (int) strlen((char*)ri->md);
		
		//int gg;
		
		//#1C20^ATT0C3A0
		while(!exit_loop){
			if(isdigit((int)ri->md[i])){
				j = 0;
				while (isdigit(ri->md[i])) {
					tmp_num[j] = ri->md[i];
					j++;
					i++;
					if(i == l){
						exit_loop = 1;
						break;
					}
				}
				tmp_num[j] = 0;
				
				c = atoi(tmp_num);
				
				//fprintf(stderr,"MD:%d\n",c);
				for(j = 0; j < c;j++){
					while(genome[gp] == -1){
						gp++;
						rp++;
					}
					while(read[rp] == -1){
						rp++;
					}
					genome[gp] = read[rp];
					
					gp++;
					rp++;
					//fprintf(stderr,"%d	%d	%d \n",aln_len,rp,i);
					while(read[rp] == -1){
						rp++;
					}
				}
				add = 0;
			}else if(isalpha((int)ri->md[i])){
				//fprintf(stderr,"MD:%c\n",ri->md[i]);
				while(genome[gp] == -1){
					gp++;
				}
				genome[gp] = nuc_code[(int)ri->md[i]];
				gp++;
				i++;
				if(!add){
					rp++;
				}
				
			}else{
				add = 1;
				i++;
			}
			if(i == l){
				exit_loop = 1;
				break;
			}
		}
		
		if(ri->strand == 0){
			gp = 0;
			for(i =0;i < aln_len;i++){
				
				if(read[i] != -1 && genome[i] != -1){
					if(read[i] != genome[i]){
						seq_stats->mismatches[qual_key][gp][read[i]] += 1;
					}
					gp++;
					//			fprintf(stderr,"Mismatch %d\n",i);
				}else if(read[i] == -1 && genome[i] != -1){
					seq_stats->deletions[qual_key][gp] += 1;
					
					//			fprintf(stderr,"Deletion %d\n",i);
				}else if(read[i] != -1 && genome[i] == -1){
					seq_stats->insertions[qual_key][gp][read[i]] += 1;
					gp++;
					//			fprintf(stderr,"Insertion %d\n",i);
				}
			}
		}else{
			gp = ri->len-1;
			for(i = 0;i < aln_len;i++){
				
				if(read[i] != -1 && genome[i] != -1){
					if(read[i] != genome[i]){
						seq_stats->mismatches[qual_key][gp][reverse_int[read[i]]] += 1;
						//		fprintf(stderr,"Mismatch %d->%d\n",i,gp);
					}
					gp--;
					
				}else if(read[i] == -1 && genome[i] != -1){
					seq_stats->deletions[qual_key][gp] += 1;
					
					//	fprintf(stderr,"Deletion %d\n",i);
				}else if(read[i] != -1 && genome[i] == -1){
					seq_stats->insertions[qual_key][gp][reverse_int[read[i]]] += 1;
					gp--;
					//	fprintf(stderr,"Insertion %d\n",i);
				}
			}
		}
		
		free(read);
		free(genome);
		return aln_len;
	}
	
	
	








