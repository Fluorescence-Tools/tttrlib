
ADD_EXECUTABLE(
        test_tttrlib
        tttr_test.cpp tttr_test.h
        ../src/tttr.cpp ../include/tttr.h
        ../include/record_reader.h ../src/record_reader.cpp
        ../src/header.cpp ../include/header.h
        ../src/histogram.cpp ../include/histogram.h
        Correlate.cpp Correlate.h
)
