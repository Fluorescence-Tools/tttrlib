
ADD_EXECUTABLE(
        test_tttrlib
        tttr_test.cpp tttr_test.h
        ../src/TTTR.cpp ../include/TTTR.h
        ../include/RecordReader.h ../src/RecordReader.cpp
        ../src/Header.cpp ../include/Header.h
        ../src/Histogram.cpp ../include/Histogram.h
        Correlate.cpp Correlate.h
)
