export curr_dir=`pwd`
cd $HOME/mdbci
$HOME/mdbci/mdbci snapshot take --path-to-nodes $1 --snapshot-name $2
cd $curr_dir

