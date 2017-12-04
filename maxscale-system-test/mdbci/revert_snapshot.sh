export curr_dir=`pwd`
cd $HOME/mdbci
$HOME/mdbci/mdbci snapshot revert --path-to-nodes $1 --snapshot-name $2
cd $curr_dir

