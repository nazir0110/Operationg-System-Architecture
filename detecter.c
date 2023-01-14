#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#define	DEFAULT_INTERVAL	(10*1000)
#define	DEFAULT_LIMIT		0
#define	DEFAULT_CHECK_EXIT	0
#define	DEFAULT_TIMEFMT		NULL

#define MAXBUF	1024

const char *progname ;

void setprogname (const char *argv0)
{
    const char *p ;

    progname = argv0 ;
    for (p = argv0 ; *p != '\0' ; p++)
    {
	if (*p == '/')
	    progname = p + 1 ;
    }
}

void raler (int perr, const char *fmt, ...)
{
    va_list ap ;

    fprintf (stderr, "%s: ", progname) ;
    va_start (ap, fmt) ;
    vfprintf (stderr, fmt, ap) ;
    va_end (ap) ;
    fprintf (stderr, "\n") ;

    if (perr)
	perror ("") ;
    exit (1) ;
}

void usage (void)
{
    raler (0, "usage: %s [-c][-i ms][-l n][-t format] prog arg..."
    		"\n    -c : check exit code"
		"\n    -i : interval in milliseconds"
		"\n    -l : limit count"
    		"\n    -t : time format (see strftime())"
		, progname) ;
}

void afficher_heure (const char *fmt)
{
    time_t t ;
    struct tm *tm ;
    char buf [MAXBUF] ;

    if (fmt != NULL)
    {
	t = time (NULL) ;
	tm = localtime (&t) ;
	if (strftime (buf, sizeof buf, fmt, tm) == 0)
	    raler (0, "Time format too long for (> %d bytes)", sizeof buf) ;
	printf ("%s\n", buf) ;
    }
}


void recuperer_sortie (int fd, char **sortie, size_t *len, int *exitcode)
{
    ssize_t n ;
    size_t buflen ;			/* taille actuelle du buffer */
    int raison ;

    *len = 0 ;
    buflen = 0 ;
    *sortie = NULL ;

    do
    {
	if (buflen - *len < MAXBUF)
	{
	    buflen += MAXBUF ;
	    *sortie = realloc (*sortie, buflen) ;
	    if (*sortie == NULL)
		raler (1, "cannot realloc %d bytes", buflen) ;
	}
	n = read (fd, *sortie + *len, buflen - *len) ;
	if (n > 0)
	    *len += n ;
    } while (n > 0) ;

    if (wait (&raison) == -1)
	raler (1, "cannot wait") ;

    if (! WIFEXITED (raison))
	raler (0, "process exited for a reason other than exit") ;

    *exitcode = WEXITSTATUS (raison) ;
}

int main (int argc, char *argv [])
{
    const char *timefmt = DEFAULT_TIMEFMT ;
    int check_exit = DEFAULT_CHECK_EXIT ;
    int limit = DEFAULT_LIMIT ;
    useconds_t interval = DEFAULT_INTERVAL * 1000 ;
    int n ;
    int opt ;
    int tube [2] ;
    char *prevstdout, *curstdout ;
    size_t prevlen, curlen ;
    int prevexit, curexit ;

    setprogname (argv [0]) ;

    while ((opt = getopt (argc, argv, "+ci:l:t:")) != -1)
    {
	int opti ;

	switch (opt)
	{
	    case 'c' :
		check_exit = 1 ;
		break ;
	    case 'i' :
		opti = atoi (optarg) ;
		if (opti <= 0)
		    usage () ;
		interval = opti * 1000 ;
		break ;
	    case 'l' :
		limit = atoi (optarg) ;
		if (limit < 0)
		    usage () ;
		break ;
	    case 't' :
		timefmt = optarg ;
		break ;
	    default :
		usage () ;
	}
    }

    if (optind >= argc)
	usage () ;

    prevstdout = NULL ;

    n = 0 ;
    while (limit == 0 || n < limit)
    {
	pipe (tube) ;

	switch (fork ())
	{
	    case -1 :
		raler (1, "cannot fork at iteration %d", n) ;
		break ;
	    case 0 :
		close (tube [0]) ;
		dup2 (tube [1], 1) ;
		close (tube [1]) ;
		execvp (argv [optind], argv + optind) ;
		raler (1, "cannot exec %s at iteration %d", argv [optind], n) ;
		break ;
	    default :
		close (tube [1]) ;
	}

	recuperer_sortie (tube [0], &curstdout, &curlen, &curexit) ;
	close (tube [0]) ;

	afficher_heure (timefmt) ;

	if (prevstdout == NULL)
	{
	    /* premier appel : on affiche tout */
	    fwrite (curstdout, curlen, 1, stdout) ;
	    if (check_exit)
		printf ("exit %d\n", curexit) ;
	}
	else
	{
	    /* appels suivants : afficher seulement les différences */
	    if (prevlen != curlen || memcmp (prevstdout, curstdout, curlen) != 0)
		fwrite (curstdout, curlen, 1, stdout) ;
	    free (prevstdout) ;

	    if (check_exit && prevexit != -1 && prevexit != curexit)
		printf ("exit %d\n", curexit) ;
	}

	prevexit = curexit ;
	prevlen = curlen ;
	prevstdout = curstdout ;

	usleep (interval) ;
	n++ ;
    }

    free (prevstdout) ;

    exit (0) ;
}
