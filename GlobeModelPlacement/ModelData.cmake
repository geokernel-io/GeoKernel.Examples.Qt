include("${CMAKE_CURRENT_LIST_DIR}/cmake/AcquireSampleData.cmake")
geokernel_acquire_sample_data(sagrada_familia_3d_model.zip
    d7f031059b2dc3fb22ce8d25dd3d7bd3d71b5f7675489c818309a78a068dd956
    "${CMAKE_BINARY_DIR}/data/sagrada-familia")
geokernel_deploy_sample_data(GlobeModelPlacement)
