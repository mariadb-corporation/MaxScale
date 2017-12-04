. ${script_dir}/set_env.sh $name

${script_dir}/backend/setup_repl.sh
${script_dir}/backend/galera/setup_galera.sh

${script_dir}/configure_core.sh

rm ~/vagrant_lock

