$file = 'zout/boot2.cmd';
open(BIN, "<$file") || die "open $file: $!\n";
binmode(BIN);
$/ = undef;
$_ = <BIN>;
close BIN;

$sz = length($_);
$_ = unpack('H*', $_);

s/(.{32})/$1\n/g;

s/(..)/0x$1,/g;
s/,\s*$//;

open(OUT, ">boot2.h");
print OUT "unsigned char boot2[$sz] = {\n$_\n};\n";
close OUT;
