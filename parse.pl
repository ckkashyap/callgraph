#!/usr/bin/perl 

use strict;
use warnings;

my$f=$ARGV[0];

my %symbols;
my @list;
my$threadCount = 0;
while(<>) {
    chomp;
    s/\s*$//;
    if (/^Total number of threads = (\S+)/) {
	$threadCount=$1;
	@list=<>;
	last;
    }
    my($address, $name, $image)=split /,/;
    $symbols{$address} = $name;
}


print "$threadCount\n";

for my $i (0 .. $threadCount-1) {
    open FILE, "$f.$i";
    open OUT, ">$f.$i.processed";
    my$tab=0;
    while(<FILE>){
	chomp;
	s/\s*$//;
	my($address,$e,$ctr)=split /,/;
	my$name=$symbols{$address} || "NOT FOUND";

	if($e){
	    print  OUT " "x$tab;
	    print  OUT "$name $e $ctr\n";
	    $tab++;
	}else {
	    $tab--;
	    print  OUT " "x$tab;
	    print  OUT "$name $e $ctr\n";

	}

	
    }
}
