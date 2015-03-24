open(my $in,"<",@ARGV[0]);
open(my $out,">",@ARGV[1]);
my $tbl = 0;
while(<$in>){
    if(/<table>/)
    {
        $tbl = 1;
    }
    elsif(/<\/table>/)
    {
        $tbl = 0;
    }
    else
    {
        if($tbl == 1)
        {
            s/\n//;
            s/<\/tr>/\n/;
            s/<td>//;
            s/<tr>//;
            s/<\/td>/\t/;
            s/^ +//;
            s/ +$//;
        }
        s/[*]/\t/;
        print $out "$_";
    }
}

