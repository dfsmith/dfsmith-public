#include <stdio.h>
#include <stdlib.h>
#include <math.h>

double rhtoah(int rev,double rhpc,double degC) {
	/* rev to reverse arguments (horrible hack) */
	/* rhpc is relative humidity, in percent */
	/* degC is the temperature in degrees Celsius */
	const double c=2.16679;
	const double tc=647.096;
	const double tt=273.16;
	const double pc=22064000;
	const double c1=-7.85951783;
	const double c2=1.84408259;
	const double c3=-11.7866497;
	const double c4=22.6807411;
	const double c5=-15.9618719;
	const double c6=1.80122502;
	double pw,pws,K,eta,coeff;
	
	if (!rev) {K=degC+tt; }
	else      {K=rhpc+tt; rhpc=degC;}
	eta=1.0-(K/tc);
	coeff =c1*eta;
	coeff+=c2*pow(eta,1.5);
	coeff+=c3*pow(eta,3);
	coeff+=c4*pow(eta,3.5);
	coeff+=c5*pow(eta,4);
	coeff+=c6*pow(eta,7.5);
	pws=pc*exp((tc/K) * coeff);
	pw=pws*rhpc*0.01;
	return c*pw/K; /* grams per cubic meter */
}

int main(int argc,char *argv[]) {
	const char *progname;
	int rev=0;
	FILE *in=stdin;
	double rh,degC;
	char line[128];
	
	progname=*argv++; argc--;
	if (argc>0 && argv[0][0]=='-' && argv[0][1]=='r') {
		argv++; argc--;
		rev=1;
	}
	while(argc>=2) {
		rh=strtod(*argv++,NULL); argc--;
		degC=strtod(*argv++,NULL); argc--;
		printf("%f\n",rhtoah(rev,rh,degC));
		in=NULL;
	}
	if (argc>0) {
		printf("Syntax: %s [-r] [relative_humidity_percent temperature_Celsius]\n",progname);
		return 1;
	}
	if (!in) return 0;
	
	while(fgets(line,sizeof(line),in)) {
		if (sscanf(line,"%lf %lf",&rh,&degC)!=2) continue;
		printf("%f\n",rhtoah(rev,rh,degC));
	}
	fclose(in);
	return 0;
}
