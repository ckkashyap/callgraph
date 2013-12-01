#!/usr/bin/perl 

use strict;
use warnings;

my %symbols;
my @list;
while(<>) {
    chomp;
    s/\s*$//;
    if (/^Total number of threads = /) {
	@list=<>;
	last;
    }
    my($address, $name, $image)=split /,/;
    $symbols{$address} = $name;
}

my$tab=0;
for(@list){
    chomp;
    s/\s*$//;
    if (/^Thread #/) {
	print  "-"x80,"\n";
	print  "$_\n";
	print  "-"x80,"\n";
	$tab=0;
	next;
    }
    my($address,$e,$ctr)=split /,/;
    my$name=$symbols{$address} || "NOT FOUND";

    if($e){
	print  " "x$tab;
	print  "$name $e $ctr\n";
	$tab++;
    }else {
	$tab--;
	print  " "x$tab;
	print  "$name $e $ctr\n";

    }

    
}
