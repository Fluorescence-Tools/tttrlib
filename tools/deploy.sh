source activate
mamba install anaconda-client
if [[ "$CI_COMMIT_REF_NAME" == "master" ]]; then DEPLOY_LABEL=main; else DEPLOY_LABEL=nightly; fi
anaconda -t ${ANACONDA_API_TOKEN} upload -l ${DEPLOY_LABEL} -u ${CONDA_USER} --skip bld-dir/**/*.tar.bz2
