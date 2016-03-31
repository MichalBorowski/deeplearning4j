/*
 * broadcasting.h
 *
 *  Created on: Dec 28, 2015
 *      Author: agibsonccc
 */

#ifndef BROADCASTING_H_
#define BROADCASTING_H_
#include <dll.h>
#include <sharedmem.h>
#include <shape.h>
#include <op.h>
#include <templatemath.h>
#include <helper_cuda.h>
#include <pairwise_util.h>

#ifdef __CUDACC__
#include <cuda.h>
#include <cuda_runtime.h>
#endif
#ifdef __JNI__
#include <jni.h>
#endif
namespace functions {
    namespace broadcast {

/**
 * Broadcast operation
 * for broadcasting a smaller tensor
 * along long a bigger one.
 */
        template<typename T>
        class Broadcast: public functions::ops::Op<T> {
        public:

            /**
             *
             * @param d1
             * @param d2
             * @return
             */
            virtual
#ifdef __CUDACC__
            inline __device__  __host__

#elif defined(__GNUC__)


#endif
            T op(T d1, T d2) = 0;
            /**
             *
             * @param d1
             * @return
             */
            virtual
#ifdef __CUDACC__
            inline __device__  __host__

#elif defined(__GNUC__)


#endif
            T op(T d1) = 0;

#ifdef __CUDACC__
            __inline__ __device__ void transform(
			T *x,
			int *xShapeInfo,
			T *y,
			int *yShapeInfo,
			T *result,
			int *resultShapeInfo,
			int *dimension,
			int dimensionLength) {

		int xElementWiseStride = shape::elementWiseStride(xShapeInfo);
		int yElementWiseStride = shape::elementWiseStride(yShapeInfo);

		//length for the tad
		int yLength = shape::length(yShapeInfo);
		//length for the tad
		int xLength = shape::length(xShapeInfo);

		int resultLength = shape::length(resultShapeInfo);
#pragma unroll
		for (int i = blockIdx.x * blockDim.x + threadIdx.x;
				i < resultLength;
				i += blockDim.x * gridDim.x) {
			int yOffset2 = ((i / xElementWiseStride) % yLength) * yElementWiseStride;
			result[i] = op(x[i],y[yOffset2]);

		}

	}
#endif

            /**
             * CPU execution
             * @param x the input
             * @param xShapeInfo the x shape information
             * @param y the y data
             * @param yShapeInfo the y shape information
             * @param result the result
             * @param resultShapeInfo the result shape information
             * @param dimension the dimension to broadcast along long
             * @param dimensionLength the length of the dimension buffer
             */
            virtual void exec(T *x,
                              int *xShapeInfo,
                              T *y,
                              int *yShapeInfo,
                              T *result,
                              int *resultShapeInfo,
                              int *dimension,
                              int dimensionLength) {

                int numOnes = 0;
                int onesEncountered = 0;
                int *shape = shape::shapeOf(xShapeInfo);
                int *stride = shape::stride(xShapeInfo);
                int wholeRank = shape::rank(xShapeInfo);
                bool squeezed = false;
                bool newSqueezeDimensions = false;
                for (int i = 0; i < wholeRank; i++) {
                    if (shape[i] == 1)
                        numOnes++;
                }

                //squeeze the dimensions
                if (numOnes > 0 && wholeRank > 2) {
                    int *squeezeShape = (int *) malloc(sizeof(int) * (wholeRank - numOnes));
                    int *squeezeStride = (int *) malloc(sizeof(int) * (wholeRank - numOnes));
                    squeezed = true;
                    int numEncountered = 0;
                    for (int i = 0; i < wholeRank; i++) {
                        if (shape[i] != 1) {
                            squeezeShape[numEncountered] = shape[i];
                            squeezeStride[numEncountered] = stride[i];
                            numEncountered++;
                        }
                    }


                    //for any dimensions specified that are 1,ignore them
                    int numDimensionsOne = 0;
                    for (int i = 0; i < dimensionLength; i++) {
                        if (shape[dimension[i]] == 1)
                            numDimensionsOne++;
                    }

                    if (numDimensionsOne > 0 && wholeRank > 2) {
                        int *newDimensions = (int *) malloc(sizeof(int) * dimensionLength - numDimensionsOne);
                        int newDimensionIdx = 0;
                        newSqueezeDimensions = true;
                        for (int i = 0; i < dimensionLength; i++) {
                            if (shape[dimension[i]] != 1)
                                newDimensions[newDimensionIdx++] = dimension[i] - numDimensionsOne;
                        }

                        //reduce along the new dimensions
                        dimension = newDimensions;
                        dimensionLength -= numDimensionsOne;

                    }
                    //update the stride and shape, note that this will not be a memory leak due to the pointers being declared differently
                    //the previous pointer is just a view of a pointer to be reused that was passed in
                    shape = squeezeShape;
                    stride = squeezeStride;
                    wholeRank -= numOnes;
                    //adjust dimensions
                    for (int i = 0; i < dimensionLength; i++) {
                        dimension[i] -= numOnes;
                    }

                    for (int i = 0; i < dimensionLength; i++) {
                        //didn't need to be adjusted
                        if (dimension[i] < 0)
                            dimension[i] += numDimensionsOne;
                    }

                    char order = shape::order(xShapeInfo);
                    xShapeInfo = shape::createShapeInfo(shape, stride, wholeRank);
                    xShapeInfo[shape::shapeInfoLength(wholeRank) - 1] = order;

                }


                //decompose in to several sub tads after
                //moving all dimensions (in sorted order)
                //to the back.
                //permuted version of the x shape info for setting up the tad problem
                int *tadShapeShapeInfo = shape::shapeInfoOnlyShapeAndStride(xShapeInfo, dimension, dimensionLength,
                                                                            false);
                int tads = shape::tensorsAlongDimension(xShapeInfo,dimension,dimensionLength);
                int *xShape = shape::shapeOf(tadShapeShapeInfo);
                int *xStride = shape::stride(tadShapeShapeInfo);
                int tadLength = shape::length(tadShapeShapeInfo);
                int rank = shape::rank(tadShapeShapeInfo);
                int *resultStride = shape::stride(tadShapeShapeInfo);

                if(result == x) {
#pragma omp  parallel  for
                    for (int i = 0; i < tads; i++) {
                        int offset = shape::tadOffset(i, xShapeInfo, dimension, dimensionLength);
                        T *xIter = x + offset;
                        T *resultIter = result + offset;
                        int shapeIter[MAX_RANK];
                        int coord[MAX_RANK];
                        int dim;
                        int xStridesIter[MAX_RANK];
                        int resultStridesIter[MAX_RANK];
                        int rank = shape::rank(tadShapeShapeInfo);
                        int vectorIdx = 0;

                        if (PrepareTwoRawArrayIter<T>(rank,
                                                      xShape,
                                                      xIter,
                                                      xStride,
                                                      resultIter,
                                                      resultStride,
                                                      &rank,
                                                      shapeIter,
                                                      &xIter,
                                                      xStridesIter,
                                                      &resultIter,
                                                      resultStridesIter) >= 0) {
                            ND4J_RAW_ITER_START(dim, rank, coord, shapeIter); {
                                /* Process the innermost dimension */
                                T val = this->op(xIter[0],y[vectorIdx]);
                                // printf("TAD %d x %f and y %f with vector idx %d and result %f\n",i,xIter[0],y[vectorIdx],vectorIdx,val);
                                xIter[0] = val;
                                vectorIdx+= shape::elementWiseStride(yShapeInfo);
                            }
                            ND4J_RAW_ITER_TWO_NEXT(dim,
                                                   rank,
                                                   coord,
                                                   shapeIter,
                                                   xIter,
                                                   xStridesIter,
                                                   result,
                                                   resultStridesIter);


                        }
                    }
                }
                else {

#pragma omp  parallel  for
                    for (int i = 0; i < tads; i++) {
                        int offset = shape::tadOffset(i, xShapeInfo, dimension, dimensionLength);
                        T *xIter = x + offset;
                        T *resultIter = result + offset;
                        int shapeIter[MAX_RANK];
                        int coord[MAX_RANK];
                        int dim;
                        int xStridesIter[MAX_RANK];
                        int resultStridesIter[MAX_RANK];
                        int rank = shape::rank(tadShapeShapeInfo);
                        int vectorIdx = 0;

                        if (PrepareTwoRawArrayIter<T>(rank,
                                                      xShape,
                                                      xIter,
                                                      xStride,
                                                      resultIter,
                                                      resultStride,
                                                      &rank,
                                                      shapeIter,
                                                      &xIter,
                                                      xStridesIter,
                                                      &resultIter,
                                                      resultStridesIter) >= 0) {
                            ND4J_RAW_ITER_START(dim, rank, coord, shapeIter); {
                                /* Process the innermost dimension */
                                T val = this->op(xIter[0],y[vectorIdx]);
                                // printf("TAD %d x %f and y %f with vector idx %d and result %f\n",i,xIter[0],y[vectorIdx],vectorIdx,val);
                                resultIter[0] = val;
                                vectorIdx+= shape::elementWiseStride(yShapeInfo);
                            }
                            ND4J_RAW_ITER_TWO_NEXT(dim,
                                                   rank,
                                                   coord,
                                                   shapeIter,
                                                   xIter,
                                                   xStridesIter,
                                                   result,
                                                   resultStridesIter);


                        }
                    }
                }




            }

            virtual inline
#ifdef __CUDACC__
            __host__ __device__
#endif
            void aggregateExtraParams(T **extraParamsTotal,T **extraParamsLocal) {
                //no extra params aggregation needs to happen
            }
#ifdef __CUDACC__
            inline __host__ __device__
#elif defined(__GNUC__)

#endif
            virtual ~Broadcast() {
            }
#ifdef __CUDACC__
            inline __host__ __device__
#elif defined(__GNUC__)

#endif
            Broadcast() {
            }

        };

        namespace ops {
            template<typename T>
            class Add: public  functions::broadcast::Broadcast<T> {
            public:


                /**
                 *
                 * @param d1
                 * @param d2
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1, T d2) {
                    return d1 + d2;
                }
                /**
                 *
                 * @param d1
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1) {
                    return d1;
                }
#ifdef __CUDACC__
                inline __host__ __device__
#elif defined(__GNUC__)

#endif
                virtual ~Add() {
                }
            };

            template<typename T>
            class Copy: public virtual functions::broadcast::Broadcast<T> {
            public:


                /**
                 *
                 * @param d1
                 * @param d2
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1, T d2) {
                    return d2;
                }
                /**
                 *
                 * @param d1
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1) {
                    return d1;
                }
#ifdef __CUDACC__
                inline __host__ __device__
#elif defined(__GNUC__)

#endif
                virtual ~Copy() {
                }

            };

            template<typename T>
            class Divide: public virtual functions::broadcast::Broadcast<T> {
            public:


                /**
                 *
                 * @param d1
                 * @param d2
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1, T d2) {
                    return d1 / d2;
                }
                /**
                 *
                 * @param d1
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1) {
                    return d1;
                }
#ifdef __CUDACC__
                inline __host__ __device__
#elif defined(__GNUC__)

#endif
                virtual ~Divide() {
                }

            };

            template<typename T>
            class Multiply: public virtual functions::broadcast::Broadcast<T> {
            public:


                /**
                 *
                 * @param d1
                 * @param d2
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1, T d2) {
                    return d1 * d2;
                }
                /**
                 *
                 * @param d1
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1) {
                    return d1;
                }
#ifdef __CUDACC__
                inline __host__ __device__
#elif defined(__GNUC__)

#endif
                virtual ~Multiply() {
                }

            };

            template<typename T>
            class ReverseDivide: public virtual functions::broadcast::Broadcast<T> {
            public:


                /**
                 *
                 * @param d1
                 * @param d2
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1, T d2) {
                    return d2 / d1;
                }
                /**
                 *
                 * @param d1
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1) {
                    return d1;
                }
#ifdef __CUDACC__
                inline __host__ __device__
#elif defined(__GNUC__)

#endif
                virtual ~ReverseDivide() {
                }

            };

            template<typename T>
            class ReverseSubtract: public virtual functions::broadcast::Broadcast<T> {
            public:


                /**
                 *
                 * @param d1
                 * @param d2
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1, T d2) {
                    return d2 - d1;
                }
                /**
                 *
                 * @param d1
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1) {
                    return d1;
                }
#ifdef __CUDACC__
                inline __host__ __device__
#elif defined(__GNUC__)

#endif
                virtual ~ReverseSubtract() {
                }

            };

            template<typename T>
            class Subtract: public virtual functions::broadcast::Broadcast<T> {
            public:

                /**
                 *
                 * @param d1
                 * @param d2
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1, T d2) {
                    return d1 - d2;
                }
                /**
                 *
                 * @param d1
                 * @return
                 */
                virtual
#ifdef __CUDACC__
                inline __host__  __device__

#elif defined(__GNUC__)


#endif
                T op(T d1) {
                    return d1;
                }
#ifdef __CUDACC__
                inline __host__ __device__
#elif defined(__GNUC__)

#endif
                virtual ~Subtract() {
                }

            };
        }

        template<typename T>
        class BroadcastOpFactory {
        public:

#ifdef __CUDACC__
            __host__ __device__
#endif
            BroadcastOpFactory() {
            }


            /**
             * creates an operation
             * @param op the op number to create:
             * 0: Add
             * 1: Subtract
             * 2: Multiply
             * 3: Divide
             * 4: ReverseDivide
             * 5: Reverse Subtract
             * 6: Copy
             * @return the broadcast operation
             */
#ifdef __CUDACC__
            __inline__ __host__ __device__
#endif
            Broadcast<T> * getOp(int op) {
                if (op == 0) {
                    return new functions::broadcast::ops::Add<T>();
                } else if (op == 1) {
                    return new functions::broadcast::ops::Subtract<T>();
                } else if (op == 2) {
                    return new  functions::broadcast::ops::Multiply<T>();
                } else if (op == 3) {
                    return new functions::broadcast::ops::Divide<T>();
                } else if (op == 4) {
                    return new functions::broadcast::ops::ReverseDivide<T>();
                } else if (op == 5) {
                    return new functions::broadcast::ops::ReverseSubtract<T>();
                } else if (op == 6) {
                    return new functions::broadcast::ops::Copy<T>();
                }

                return NULL;

            }

        };

    }
}

#ifdef __CUDACC__

/**
 * Meant to be called from an external interface
 * and the driver api
 * @param opNum the op number to execute
 * @param x the input data
 * @param xShapeInfo the x shape info for input
 * @param y the y to broadcast
 * @param yShapeInfo the shape information of the broadcast info
 * @param result the result buffer
 * @param resultShapeInfo the shape information for the result buffer
 * @param dimension the dimension(s) to do broadcast along long
 * @param dimensionLength the length of the dimension buffer
 * @param gpuInformation the gpu information such as blockdim,griddim and shared
 * memory size
 */
template <typename T>
__device__ void broadcastGeneric(
		int opNum,
		T *x,
		int *xShapeInfo,
		T *y,
		int *yShapeInfo,
		T *result,
		int *resultShapeInfo,
		int *dimension,
		int dimensionLength) {

	//TODO: Reduce object creation
	__shared__ functions::broadcast::Broadcast<T> *op;
	__shared__ functions::broadcast::BroadcastOpFactory<T> *newOpFactory;
	if(threadIdx.x == 0)
		newOpFactory =  new functions::broadcast::BroadcastOpFactory<T>();
	__syncthreads();
	if(threadIdx.x == 0) {
		op = newOpFactory->getOp(opNum);
	}
	__syncthreads();


	op->transform(
			x,
			xShapeInfo,
			y,
			yShapeInfo,
			result,
			resultShapeInfo,
			dimension,
			dimensionLength);

	if(threadIdx.x == 0) {
		free(op);
		free(newOpFactory);
	}
}

/**
 * Meant to be called from an external interface
 * and the driver api
 * @param opNum the op number to execute
 * @param x the input data
 * @param xShapeInfo the x shape info for input
 * @param y the y to broadcast
 * @param yShapeInfo the shape information of the broadcast info
 * @param result the result buffer
 * @param resultShapeInfo the shape information for the result buffer
 * @param dimension the dimension(s) to do broadcast along long
 * @param dimensionLength the length of the dimension buffer
 * @param gpuInformation the gpu information such as blockdim,griddim and shared
 * memory size
 */
extern "C" __global__ void broadcastDouble(
		int opNum,
		double *x, int *xShapeInfo,
		double *y, int *yShapeInfo,
		double *result, int *resultShapeInfo,
		int *dimension,
		int dimensionLength) {
	broadcastGeneric<double>(
			opNum,
			x,
			xShapeInfo,
			y,
			yShapeInfo,
			result,
			resultShapeInfo,
			dimension,
			dimensionLength);

}


/**
 * Meant to be called from an external interface
 * and the driver api
 * @param opNum the op number to execute
 * @param x the input data
 * @param xShapeInfo the x shape info for input
 * @param y the y to broadcast
 * @param yShapeInfo the shape information of the broadcast info
 * @param result the result buffer
 * @param resultShapeInfo the shape information for the result buffer
 * @param dimension the dimension(s) to do broadcast along long
 * @param dimensionLength the length of the dimension buffer
 * @param gpuInformation the gpu information such as blockdim,griddim and shared
 * memory size
 */
extern "C" __global__ void broadcastFloat(
		int opNum,
		float *x, int *xShapeInfo,
		float *y, int *yShapeInfo,
		float *result, int *resultShapeInfo,
		int *dimension,
		int dimensionLength) {
	broadcastGeneric<float>(
			opNum,
			x,
			xShapeInfo,
			y,
			yShapeInfo,
			result,
			resultShapeInfo,
			dimension,
			dimensionLength);

}

#endif



#endif /* BROADCASTING_H_ */
