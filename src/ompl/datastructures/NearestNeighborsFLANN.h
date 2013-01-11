/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2011, Rice University
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Rice University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Author: Mark Moll */

#ifndef OMPL_DATASTRUCTURES_NEAREST_NEIGHBORS_FLANN_
#define OMPL_DATASTRUCTURES_NEAREST_NEIGHBORS_FLANN_

#include "ompl/config.h"
#if OMPL_HAVE_FLANN == 0
#  error FLANN is not available. Please use a different NearestNeighbors data structure.
#else

#include "ompl/datastructures/NearestNeighbors.h"
#include "ompl/base/StateSpace.h"

#include <flann/flann.hpp>

namespace ompl
{
    /** \brief Wrapper class to allow FLANN access to the
        NearestNeighbors::distFun_ callback function
    */
    template<typename _T>
    class FLANNDistance
    {
    public:
        typedef _T ElementType;
        typedef double ResultType;

        FLANNDistance(const typename NearestNeighbors<_T>::DistanceFunction& distFun)
            : distFun_(distFun)
        {
        }

        template <typename Iterator1, typename Iterator2>
        ResultType operator()(Iterator1 a, Iterator2 b,
            size_t /*size*/, ResultType /*worst_dist*/ = -1) const
        {
            return distFun_(*a, *b);
        }
    protected:
        const typename NearestNeighbors<_T>::DistanceFunction& distFun_;
    };

    /** \brief Wrapper class for nearest neighbor data structures in the
        FLANN library.

        See:
        M. Muja and D.G. Lowe, "Fast Approximate Nearest Neighbors with
        Automatic Algorithm Configuration", in International Conference
        on Computer Vision Theory and Applications (VISAPP'09), 2009.
        http://people.cs.ubc.ca/~mariusm/index.php/FLANN/FLANN
    */
    template<typename _T, typename _Dist = FLANNDistance<_T> >
    class NearestNeighborsFLANN : public NearestNeighbors<_T>
    {
    public:

        NearestNeighborsFLANN(const boost::shared_ptr<flann::IndexParams> &params)
            : index_(0), params_(params), searchParams_(32, 0., true), dimension_(1)
        {
        }

        virtual ~NearestNeighborsFLANN(void)
        {
            if (index_)
                delete index_;
        }

        virtual void clear(void)
        {
            if (index_)
            {
                delete index_;
                index_ = NULL;
            }
            data_.clear();
        }

        virtual void setDistanceFunction(const typename NearestNeighbors<_T>::DistanceFunction &distFun)
        {
            NearestNeighbors<_T>::setDistanceFunction(distFun);
            rebuildIndex();
        }

        virtual void add(const _T &data)
        {
            bool rebuild = index_ && (data_.size() + 1 > data_.capacity());

            if (rebuild)
                rebuildIndex(2 * data_.capacity());

            data_.push_back(data);
            const flann::Matrix<_T> mat(&data_.back(), 1, dimension_);

            if (index_)
                index_->addPoints(mat, std::numeric_limits<float>::max()/size());
            else
                createIndex(mat);
        }
        virtual void add(const std::vector<_T> &data)
        {
            unsigned int oldSize = data_.size();
            unsigned int newSize = oldSize + data.size();
            bool rebuild = index_ && (newSize > data_.capacity());

            if (rebuild)
                rebuildIndex(std::max(2 * oldSize, newSize));

            if (index_)
            {
                std::copy(data.begin(), data.end(), data_.begin() + oldSize);
                const flann::Matrix<_T> mat(&data_[oldSize], data.size(), dimension_);
                index_->addPoints(mat, std::numeric_limits<float>::max()/size());
            }
            else
            {
                data_ = data;
                const flann::Matrix<_T> mat(&data_[0], data_.size(), dimension_);
                createIndex(mat);
            }
        }
        virtual bool remove(const _T& data)
        {
            if (!index_) return false;
            _T& elt = const_cast<_T&>(data);
            const flann::Matrix<_T> query(&elt, 1, dimension_);
            std::vector<std::vector<size_t> > indices(1);
            std::vector<std::vector<double> > dists(1);
            index_->knnSearch(query, indices, dists, 1, searchParams_);
            if (*index_->getPoint(indices[0][0]) == data)
            {
                index_->removePoint(indices[0][0]);
                rebuildIndex();
                return true;
            }
            return false;
        }
        virtual _T nearest(const _T &data) const
        {
            if (size())
            {
                _T& elt = const_cast<_T&>(data);
                const flann::Matrix<_T> query(&elt, 1, dimension_);
                std::vector<std::vector<size_t> > indices(1);
                std::vector<std::vector<double> > dists(1);
                index_->knnSearch(query, indices, dists, 1, searchParams_);
                return *index_->getPoint(indices[0][0]);
            }
            throw Exception("No elements found in nearest neighbors data structure");
        }
        virtual void nearestK(const _T &data, std::size_t k, std::vector<_T> &nbh) const
        {
            _T& elt = const_cast<_T&>(data);
            const flann::Matrix<_T> query(&elt, 1, dimension_);
            std::vector<std::vector<size_t> > indices;
            std::vector<std::vector<double> > dists;
            k = index_ ? index_->knnSearch(query, indices, dists, k, searchParams_) : 0;
            nbh.resize(k);
            for (std::size_t i = 0 ; i < k ; ++i)
                nbh[i] = *index_->getPoint(indices[0][i]);
        }

        virtual void nearestR(const _T &data, double radius, std::vector<_T> &nbh) const
        {
            _T& elt = const_cast<_T&>(data);
            flann::Matrix<_T> query(&elt, 1, dimension_);
            std::vector<std::vector<size_t> > indices;
            std::vector<std::vector<double> > dists;
            int k = index_ ? index_->radiusSearch(query, indices, dists, radius, searchParams_) : 0;
            nbh.resize(k);
            for (int i = 0 ; i < k ; ++i)
                nbh[i] = *index_->getPoint(indices[0][i]);
        }

        virtual std::size_t size(void) const
        {
            return index_ ? index_->size() : 0;
        }

        virtual void list(std::vector<_T> &data) const
        {
            std::size_t sz = size();
            if (sz == 0)
            {
                data.resize(0);
                return;
            }
            const _T& dummy = *index_->getPoint(0);
            int checks = searchParams_.checks;
            searchParams_.checks = size();
            nearestK(dummy, sz, data);
            searchParams_.checks = checks;
        }

        /// \brief Set the FLANN index parameters.
        ///
        /// The parameters determine the type of nearest neighbor
        /// data structure to be constructed.
        virtual void setIndexParams(const boost::shared_ptr<flann::IndexParams> &params)
        {
            params_ = params;
            rebuildIndex();
        }

        /// \brief Get the FLANN parameters used to build the current index.
        virtual const boost::shared_ptr<flann::IndexParams>& getIndexParams(void) const
        {
            return params_;
        }

        /// \brief Set the FLANN parameters to be used during nearest neighbor
        /// searches
        virtual void setSearchParams(const flann::SearchParams& searchParams)
        {
            searchParams_ = searchParams;
        }

        /// \brief Get the FLANN parameters used during nearest neighbor
        /// searches
        flann::SearchParams& getSearchParams(void)
        {
            return searchParams_;
        }

        /// \brief Get the FLANN parameters used during nearest neighbor
        /// searches
        const flann::SearchParams& getSearchParams(void) const
        {
            return searchParams_;
        }

        unsigned int getContainerSize(void) const
        {
            return dimension_;
        }

    protected:

        /// \brief Internal function to construct nearest neighbor
        /// data structure with initial elements stored in mat.
        void createIndex(const flann::Matrix<_T>& mat)
        {
            index_ = new flann::Index<_Dist>(mat, *params_, _Dist(NearestNeighbors<_T>::distFun_));
            index_->buildIndex();
        }

        /// \brief Rebuild the nearest neighbor data structure (necessary when
        /// changing the distance function or index parameters).
        void rebuildIndex(unsigned int capacity = 0)
        {
            if (index_)
            {
                std::vector<_T> data;
                list(data);
                clear();
                if (capacity)
                    data_.reserve(capacity);
                add(data);
            }
        }

        /// \brief vector of data stored in FLANN's index. FLANN only indexes
        /// references, so we need store the original data.
        std::vector<_T>                       data_;

        /// \brief The FLANN index (the actual index type depends on params_).
        flann::Index<_Dist>*                  index_;

        /// \brief The FLANN index parameters. This contains both the type of
        /// index and the parameters for that type.
        boost::shared_ptr<flann::IndexParams> params_;

        /// \brief The parameters used to seach for nearest neighbors.
        mutable flann::SearchParams           searchParams_;

        /// \brief If each element has an array-like structure that is exposed
        /// to FLANN, then the dimension_ needs to be set to the length of
        /// this array.
        unsigned int                          dimension_;
    };

    template<>
    void NearestNeighborsFLANN<double, flann::L2<double> >::createIndex(
        const flann::Matrix<double>& mat)
    {
        index_ = new flann::Index<flann::L2<double> >(mat, *params_);
        index_->buildIndex();
    }

#define OMPL_NEAREST_NEIGHBORS_FLANN_CLASS(T)                                          \
    template<typename _T>                                                              \
    class NearestNeighborsFLANN##T : public NearestNeighborsFLANN<_T>                  \
    {                                                                                  \
    public:                                                                            \
        NearestNeighborsFLANN##T()                                                     \
            : NearestNeighborsFLANN<_T>(                                               \
                boost::shared_ptr<flann::T##IndexParams>(new flann::T##IndexParams())) \
        {                                                                              \
        }                                                                              \
    };

    OMPL_NEAREST_NEIGHBORS_FLANN_CLASS(Linear);
    OMPL_NEAREST_NEIGHBORS_FLANN_CLASS(HierarchicalClustering);

    // Wrappers for the FLANN indices below need a different distance class
    // that assumes the data type _T for which a nearest neighbor structure
    // is built has an array-like structure; see the FLANN manual for details.
    // We predefine some for elements that look like double*. Note that you
    // need to insert in this case an element by reference to the first element.
    // E.g., if your element s is of type std::vector<double>, then you'd add
    // s[0] (which is of type double&). Keep in mind that s needs to stay in
    // scope for the lifetime of the nearest neighbors object.
#define OMPL_NEAREST_NEIGHBORS_FLANN_KDTREE_CLASS(T)                                   \
    class NearestNeighborsFLANN##T                                                     \
        : public NearestNeighborsFLANN<double, flann::L2<double> >                     \
    {                                                                                  \
    public:                                                                            \
        NearestNeighborsFLANN##T(unsigned int dim)                                     \
            : NearestNeighborsFLANN<double, flann::L2<double> >(                       \
                boost::shared_ptr<flann::T##IndexParams>(new flann::T##IndexParams())) \
        {                                                                              \
            dimension_ = dim;                                                          \
        }                                                                              \
    };

    OMPL_NEAREST_NEIGHBORS_FLANN_KDTREE_CLASS(KDTree);
    OMPL_NEAREST_NEIGHBORS_FLANN_KDTREE_CLASS(KMeans);
    OMPL_NEAREST_NEIGHBORS_FLANN_KDTREE_CLASS(Composite);
    OMPL_NEAREST_NEIGHBORS_FLANN_KDTREE_CLASS(KDTreeSingle);
#ifdef FLANN_USE_CUDA
    OMPL_NEAREST_NEIGHBORS_FLANN_KDTREE_CLASS(KDTreeCuda3d);
#endif

}
#endif

#endif
