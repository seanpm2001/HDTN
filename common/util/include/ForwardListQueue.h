/**
 * @file ForwardListQueue.h
 * @author  Brian Tomko <brian.j.tomko@nasa.gov>
 *
 * @copyright Copyright � 2021 United States Government as represented by
 * the National Aeronautics and Space Administration.
 * No copyright is claimed in the United States under Title 17, U.S.Code.
 * All Other Rights Reserved.
 *
 * @section LICENSE
 * Released under the NASA Open Source Agreement (NOSA)
 * See LICENSE.md in the source root directory for more information.
 *
 * @section DESCRIPTION
 *
 * This ForwardListQueue class provides minimal empty sized Queue (non-copyable).
 */

#ifndef _FORWARD_LIST_QUEUE_H
#define _FORWARD_LIST_QUEUE_H 1

#include <forward_list>
#include <cstdint>
#include <memory>
#include <boost/core/noncopyable.hpp>
#include "hdtn_util_export.h"

template<typename T>
class ForwardListQueue : private boost::noncopyable {
public:
    typedef typename std::forward_list<T>::iterator iterator;
    typedef typename std::forward_list<T>::const_iterator const_iterator;
private:
    std::forward_list<T> m_fl;
    iterator m_flLastIt;
public:
    ForwardListQueue() = default;
    ForwardListQueue(std::initializer_list<T> init)
        : m_fl(init)
    { }

    template <class... Args>
    void emplace_back(Args&&... args) {
        //insert into list (order by FIFO, so newest elements will be last)
        if (m_fl.empty()) {
            m_fl.emplace_front(std::forward<Args>(args)...);
            m_flLastIt = m_fl.begin();
        }
        else {
            m_flLastIt = m_fl.emplace_after(m_flLastIt, std::forward<Args>(args)...);
        }
    }

    void push_back(const T & val) {
        //insert into list (order by FIFO, so newest elements will be last)
        if (m_fl.empty()) {
            m_fl.push_front(val);
            m_flLastIt = m_fl.begin();
        }
        else {
            m_flLastIt = m_fl.insert_after(m_flLastIt, val);
        }
    }

    void push_back(T&& val) {
        //insert into list (order by FIFO, so newest elements will be last)
        if (m_fl.empty()) {
            m_fl.push_front(std::move(val));
            m_flLastIt = m_fl.begin();
        }
        else {
            m_flLastIt = m_fl.insert_after(m_flLastIt, std::move(val));
        }
    }

    template <class... Args>
    void emplace_front(Args&&... args) {
        const bool wasEmpty = m_fl.empty();
        m_fl.emplace_front(std::forward<Args>(args)...);
        if (wasEmpty) {
            m_flLastIt = m_fl.begin();
        }
    }

    void push_front(const T& val) {
        const bool wasEmpty = m_fl.empty();
        m_fl.push_front(val);
        if (wasEmpty) {
            m_flLastIt = m_fl.begin();
        }
    }

    void push_front(T&& val) {
        const bool wasEmpty = m_fl.empty();
        m_fl.push_front(std::move(val));
        if (wasEmpty) {
            m_flLastIt = m_fl.begin();
        }
    }

    T& front() noexcept {
        return m_fl.front();
    }

    const T& front() const noexcept {
        return m_fl.front();
    }

    T& back() noexcept {
        return *m_flLastIt;
    }

    const T& back() const noexcept {
        return *m_flLastIt;
    }

    void pop() noexcept {
        m_fl.pop_front();
    }

    void clear() noexcept {
        m_fl.clear();
    }

    bool empty() const noexcept {
        return m_fl.empty();
    }

    bool operator==(const ForwardListQueue& o) const {
        return (m_fl == o.m_fl);
    }

    bool operator!=(const ForwardListQueue& o) const {
        return (m_fl != o.m_fl);
    }

    const std::forward_list<T>& GetUnderlyingForwardListRef() const noexcept {
        return m_fl;
    }

    iterator begin() const noexcept {
        return m_fl.begin();
    }

    iterator end() const noexcept {
        return m_fl.end();
    }

    const_iterator cbegin() const noexcept {
        return m_fl.cbegin();
    }

    const_iterator cend() const noexcept {
        return m_fl.cend();
    }
    
};

#endif //_FORWARD_LIST_QUEUE_H
