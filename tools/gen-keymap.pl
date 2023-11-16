#!/bin/perl

use strict;
use warnings;

if (@ARGV != 2) {
    die "Usage: tools/gen-keymap.pl /usr/include/linux/input-event-codes.h third-party/gdk/generated/keynamesprivate.h > generated/keymap.h\n";
}



open IN, $ARGV[0] or die "Cannot open $ARGV[0]: $!\n";

my %keycodes = ();
my %translations = (
    'CTRL' => 'CONTROL',
    'META' => 'SUPER'
);
while (<IN>) {
    next if (not /^#define/);

    /(KEY_((?:KP|FN_)?(?:(LEFT|RIGHT)([^_][\w_]*))?(\4|[\w_]*)))/;
    next if (not defined $1 or $1 =~ /(?:MIN_INTERESTING|MAX|CNT)$/);

    my $keyname = $2;
    if (defined $3) {
        if ($4 eq 'CTRL' or $4 eq 'SHIFT' or $4 eq 'ALT' or $4 eq 'META') {
            next if (not $3 eq 'LEFT');
            $keyname = exists $translations{$4} ? $translations{$4} : $4;
        } else {
            $keyname = $4 . $3;
        }
    }

    $keycodes{lc $keyname} = $1;
}

close IN;
undef %translations;



open IN, $ARGV[1] or die "Cannot open $ARGV[1]: $!\n";

my $start = 0;
my %keynames = ();
while (<IN>) {
    if (not $start) {
        next if (not /char\s*keynames\s*\[\s*\]\s*=\s*$/);
        $start = 1;
        next;
    }
    last if /;$/;

    /\"(?:(?:([\w_]*)(?:_(L|R)))|([\w]*))\\/;
    my $keyname = lc(defined $1 ? $1 : $3);
    next if ((defined $2 and $2 eq 'R') or not exists $keycodes{$keyname});
    $keynames{$keyname} = ();
}

close IN;
undef $start;



my $time = gmtime;
print <<EOF;
/* Generated by gen-keymap.pl at $time
 *
 * Do not edit.
 */

#ifndef MZD_GEN_KEYCODE_H
#define MZD_GEN_KEYCODE_H

#include <linux/input-event-codes.h>

EOF
my @keynames = sort keys %keynames;
my $first;

print '#define MZD_KEYMAP_LEN ' . scalar @keynames . "u\n\n";

print 'static const char *mzd_keymap_names[] = {';
$first = 1;
foreach my $keyname (@keynames) {
    if ($first) {
        $first = 0;
    } else {
        print(',');
    }
    print qq(\n    "$keyname");
}
undef $first;
print "\n};\n\n";

print 'static const unsigned int mzd_keymap_codes[] = {';
$first = 1;
foreach my $keyname (@keynames) {
    if ($first) {
        $first = 0;
    } else {
        print(',');
    }
    print "\n    $keycodes{$keyname}";
}
undef $first;
print "\n};\n\n";

print "#endif";
