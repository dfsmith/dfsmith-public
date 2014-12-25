/* > dfsread.c */
/**\file
 * Simple read and extract Acorn DFS .ssd files.
 */
#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned int uint;
typedef u8 bool;

typedef uint dfs_sector;

typedef struct {
	char name[8];
	char directory[2];
	uint exec,load;
	uint length;
	bool locked,noread,nowrite,noexec,dir;
	dfs_sector start;
} dfs_file;

typedef struct {
	char title[13];
	dfs_sector sectors;
	uint bootopt;
	uint cycles;
	uint lastfile,lastfileremainder;
	dfs_file file[31];
} dfs_disk;

static void locpy(char *dest,const u8 *src,size_t len) {
	int i;
	for(i=0;i<len;i++) *(dest++)=*(src++) & 0x7F;
}

static const char *filecopy(FILE *dest,FILE *src,size_t len) {
	u8 block[0x100];
	while(len>0) {
		size_t b;
		b=(len < sizeof(block))?len:sizeof(block);
		if (fread(block,1,b,src) != b) return "bad read";
		if (fwrite(block,1,b,dest) != b) return "bad write";
		len-=b;
	}
	return NULL;
}

static const char *writefile(FILE *ssd,const dfs_file *f) {
	char n[100]={};
	FILE *w;
	const char *err;

	if (fseek(ssd,f->start * 0x100,SEEK_SET) != 0)
		return "cannot seek";
	
	strcat(n,"dfs_");
	if (f->directory[0]!='$') {
		strcat(n,f->directory);
		strcat(n,".");
	}
	strcat(n,f->name);
	for(;;) {
		int last;
		last=strlen(n)-1;
		if (last<0) return "empty filename";
		if (n[last]!=' ') break;
		n[last]='\0';
	}
	
	w=fopen(n,"wb");
	if (!w) return "cannot open file for writing";
	err=filecopy(w,ssd,f->length);
	fclose(w);

	return err;
}

static const char *cat(const char *filename) {
	const char *err=NULL;
	u8 cat[0x200];
	FILE *ssd;
	dfs_disk d={};
	
	ssd=fopen(filename,"rb");
	if (!ssd) return "cannot open file";

	do {
		size_t n;
		int i;
		
		n=fread(cat,0x100,2,ssd);
		if (n!=2) {err="short catalog read"; break;}
		
		locpy(d.title+0,cat+0x000,8);
		locpy(d.title+8,cat+0x100,4);
		d.title[13]='\0';

		d.cycles=cat[0x104];
		
		d.lastfile=cat[0x105]/8;
		d.lastfileremainder=cat[0x105]%8;
		
		d.bootopt=(cat[0x106]>>4) & 0x03;
		
		d.sectors=cat[0x107];
		d.sectors|=(cat[0x106]&15)<<8;
		
		for(i=0;i<31;i++) {
			dfs_file *f=&d.file[i];
			u8 *loc=cat+8+i*8;
			locpy(f->name,loc,7);
			f->name[8]='\0';
			locpy(f->directory,loc+7,1);
			f->directory[1]='\0';

			f->load=loc[0x101]<<8 | loc[0x100]<<0;
			f->load|=((loc[0x106]>>2) & 3) << 16;
			f->exec=loc[0x103]<<8 | loc[0x102]<<0;
			f->exec|=((loc[0x106]>>6) & 3) << 16;
			f->length=loc[0x105]<<8 | loc[104]<<0;
			f->length|=((loc[0x106]>>4) & 3) << 16;
			f->length|=((loc[0x001]>>7) & 1) << 18;
			f->start=loc[0x107];
			f->start|=((loc[0x106]>>0) & 3) << 8;
			f->start|=((loc[0x000]>>7) & 1) << 9;
			f->dir=!!(loc[0x3]&0x80);
			f->noread=!!(loc[0x4]&0x80);
			f->nowrite=!!(loc[0x5]&0x80);
			f->noexec=!!(loc[0x6]&0x80);
			f->locked=!!(loc[0x7]&0x80);
		}
		
		printf("Volume: %s\n",d.title);
		printf("Sectors: 0x%X\n",d.sectors);
		printf("Cycles: %u\n",d.cycles);
		printf("Boot: %u\n",d.bootopt);
		printf("Files: %u r%u\n",d.lastfile,d.lastfileremainder);
		
		for(i=0;!err && i<31;i++) {
			dfs_file *f=&d.file[i];
			if (i>=d.lastfile) continue;
			printf("%s%.1s.%.7s 0x%.5X 0x%.5X start 0x%.3X%s%s%s%s\n",
				(i>=d.lastfile)?"(empty) ":"",
				f->directory,f->name,
				f->load,f->exec,f->start,
				f->locked?" L":"",
				f->noread?" noread":"",
				f->nowrite?" nowrite":"",
				f->noexec?" noexec":"");
			err=writefile(ssd,f);
		}
	} while(0);	
	fclose(ssd);
	return err;
}

int main(int argc,char *argv[]) {
	char *progname;
	const char *err=NULL;
	
	progname=*(argv++); argc--;
	for(;!err && argc>0;argv++,argc--) {
		err=cat(*argv);
	}
	if (err) {
		fprintf(stderr,"%s: %s\n",progname,err);
		return 1;
	}
	return 0;
}
