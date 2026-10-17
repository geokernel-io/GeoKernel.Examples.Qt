include("${CMAKE_CURRENT_LIST_DIR}/cmake/AcquireSampleData.cmake")
geokernel_acquire_sample_data(sagrada_familia_buildings.zip
    e1f7aeff6701fcd35696620e49df767ad10f0a9778b19eb2d9ea47d587dad017
    "${CMAKE_BINARY_DIR}/data")
geokernel_deploy_sample_data(GlobeVectorStyling)
