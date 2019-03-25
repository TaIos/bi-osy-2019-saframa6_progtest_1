#include <utility>

#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <vector>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "progtest_solver.h"
#include "sample_tester.h"

using namespace std;
#endif /* __PROGTEST__ */

struct Task {
    unsigned id_material;
    ACustomer customer;
    AOrderList orderList;

    Task(unsigned id_material, const ACustomer &customer, const AOrderList &orderList) : id_material(id_material),
                                                                                         customer(customer),
                                                                                         orderList(orderList) {}
};

struct BestPriceList {
    int id_material;
    int pricingsLeft;
    map<unsigned, map<unsigned, double>> best;
    mutex lock;
    CPriceList priceList;
    APriceList a;

    BestPriceList(unsigned id_material, int pricingsLeft) : id_material(id_material), pricingsLeft(pricingsLeft),
                                                            priceList(CPriceList(id_material)) {}

    BestPriceList() : priceList(CPriceList(0)) {}

    void updateBestPriceList(const vector<CProd> &list) {
        for (const auto &e : list) {
            auto firstMap = best.find(e.m_W);
            if (firstMap == best.end()) {
                best[e.m_W][e.m_H] = e.m_Cost;
            } else {
                auto secondMap = firstMap->second;
                auto it = secondMap.find(e.m_H);
                if (it == secondMap.end())
                    firstMap->second[e.m_H] = e.m_Cost;
                else if (it->second > e.m_Cost)
                    it->second = e.m_Cost;
            }
        }
    }

    APriceList &createGetPriceList() {
        priceList.m_List.clear();
        for (auto i: best) {
            for (auto j: i.second) {
                priceList.Add(CProd(i.first, j.first, j.second));
            }
        }
        a = make_shared<CPriceList>(priceList);
        return a;
    }

    BestPriceList &operator=(const BestPriceList &other) {
        if (this != &other) {
            id_material = other.id_material;
            pricingsLeft = other.pricingsLeft;
            best = other.best;
            priceList = other.priceList;
        }
        return *this;
    }
};

struct PriceListWrapper {
    AProducer prod;
    APriceList priceList;
    bool end;

    PriceListWrapper(const AProducer &prod, const APriceList &priceList) : prod(prod), priceList(priceList),
                                                                           end(false) {}

    PriceListWrapper() : end(false) {}

    PriceListWrapper(bool end) : end(end) {}
};

class CWeldingCompany {
private:
    int activeCustomerCnt;

    map<int, vector<Task>> taskPool;
    deque<PriceListWrapper> priceListPool;
    map<int, BestPriceList> bestPriceList;

    vector<AProducer> producers;
    vector<ACustomer> customers;

    vector<thread> threadsCustomer;
    vector<thread> threadsWorker;

    mutex mtx_taskPoolManip;
    mutex mtx_priceListPoolManip;
    mutex mtx_bestPriceListPoolManip;
    mutex mtx_customerActiveCntManip;

    condition_variable cv_priceListPoolEmpty; // protects from removing from empty pool
    condition_variable cv_taskPoolEmpty; // signals when TaskPool is empty

    //================================================================================
    PriceListWrapper popFromPricelist();

    void preprocessPricelist(PriceListWrapper &priceListWrapper);

    map<int, BestPriceList>::iterator findCreateBestPriceList(PriceListWrapper &priceListWrapper);

    void updateBestPriceList(const vector<CProd> &pricelist, BestPriceList &best);

public:
    void customerRoutine(ACustomer customer);

    void workerRoutine();

    void AddProducer(AProducer prod);

    void AddCustomer(ACustomer cust);

    void AddPriceList(AProducer prod, APriceList priceList);

    void Start(unsigned thrCount);

    void Stop();

    static void SeqSolve(APriceList priceList, COrder &order);
};

void CWeldingCompany::customerRoutine(ACustomer customer) {
    AOrderList orderList;

    while (true) {
        orderList = customer.get()->WaitForDemand();
        if (orderList == nullptr || orderList.get() == nullptr)
            break;

        Task task(orderList.get()->m_MaterialID, customer, orderList);

        unique_lock<mutex> ul(mtx_taskPoolManip);
        auto it = taskPool.find(task.id_material);
        if (it == taskPool.end()) {
            vector<Task> vec;
            vec.emplace_back(task);
            taskPool[task.id_material] = vec;
        } else {
            it->second.emplace_back(task);
        }
        ul.unlock();

        for (const auto &e : producers)
            e.get()->SendPriceList(task.id_material);
    }
    unique_lock<mutex> lock_CustomerActiveCnt(mtx_customerActiveCntManip);
    activeCustomerCnt--;
    if (activeCustomerCnt == 0) { // last active customerThread
        unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
        cv_priceListPoolEmpty.wait(lock_TaskPool, [this]() { return priceListPool.empty(); });
        // last PriceList from Producers, add ending tokens
        unique_lock<mutex> lock_PriceListPool(mtx_priceListPoolManip);
        for (int i = 0; i < (int) threadsWorker.size(); i++) {
            priceListPool.emplace_back(PriceListWrapper(true));
        }
        lock_PriceListPool.unlock();
        lock_TaskPool.unlock();
        cv_priceListPoolEmpty.notify_all(); // wake up all sleeping workerThreads
    }
    lock_CustomerActiveCnt.unlock();
}

void CWeldingCompany::workerRoutine() {
    PriceListWrapper currPriceList;
    while (true) {

        currPriceList = popFromPricelist();

        if (currPriceList.end)
            break;

        preprocessPricelist(currPriceList);
        auto it_best = findCreateBestPriceList(currPriceList);
        updateBestPriceList(currPriceList.priceList.get()->m_List, it_best->second);
    }
}

void CWeldingCompany::AddPriceList(AProducer prod, APriceList priceList) {
    unique_lock<mutex> ul(mtx_priceListPoolManip);
    priceListPool.emplace_back(PriceListWrapper(prod, priceList));
    cv_priceListPoolEmpty.notify_one();
    ul.unlock();
}

void CWeldingCompany::AddProducer(AProducer prod) {
    producers.emplace_back(prod);
}

void CWeldingCompany::AddCustomer(ACustomer cust) {
    customers.emplace_back(cust);
}

void CWeldingCompany::Start(unsigned thrCount) {
    activeCustomerCnt = (int) customers.size();
    for (auto &c : customers)
        threadsCustomer.emplace_back([=] { customerRoutine(c); });
    for (unsigned i = 0; i < thrCount; i++)
        threadsWorker.emplace_back([=] { workerRoutine(); });
}

void CWeldingCompany::Stop() {
    for (auto &t : threadsCustomer)
        t.join();
    for (auto &t: threadsWorker)
        t.join();
}

void CWeldingCompany::SeqSolve(APriceList priceList, COrder &order) {
    vector<COrder> wrapper;
    wrapper.emplace_back(order);
    ProgtestSolver(wrapper, std::move(priceList));
}

PriceListWrapper CWeldingCompany::popFromPricelist() {
    PriceListWrapper currPriceList;
    unique_lock<mutex> lock_PriceListPool(mtx_priceListPoolManip);
    cv_priceListPoolEmpty.wait(lock_PriceListPool, [this]() { return !priceListPool.empty(); });
    currPriceList = priceListPool.front();
    priceListPool.pop_front();
    lock_PriceListPool.unlock();
    return currPriceList;
}

void CWeldingCompany::preprocessPricelist(PriceListWrapper &priceListWrapper) {
// make CurrPriceList consistent (m_W <= m_H is always true)
    unsigned int tmp;
    for (auto &e : priceListWrapper.priceList.get()->m_List) {
        if (e.m_W > e.m_H) {
            tmp = e.m_W;
            e.m_W = e.m_H;
            e.m_H = tmp;
        }
    }
}

map<int, BestPriceList>::iterator CWeldingCompany::findCreateBestPriceList(PriceListWrapper &priceListWrapper) {
    // Find BestPriceList for updating, construct it if record doesn't exist
    map<int, BestPriceList>::iterator it;
    int id = priceListWrapper.priceList.get()->m_MaterialID;

    unique_lock<mutex> lock_BestPriceListPool(mtx_bestPriceListPoolManip);
    it = bestPriceList.find(id);
    if (it == bestPriceList.end()) {
        bestPriceList[id] = BestPriceList(static_cast<unsigned int>(id), (int) producers.size());
    }
    lock_BestPriceListPool.unlock();
    return it;
}

void CWeldingCompany::updateBestPriceList(const vector<CProd> &pricelist, BestPriceList &best) {
    // update BestPriceList with given PriceList
    unique_lock<mutex> lock_BestPriceListRecord(best.lock);
    best.updateBestPriceList(pricelist);

    // all pricings collected
    if ((--best.pricingsLeft) == 0) {
        unique_lock<mutex> lock_TaskPool(mtx_taskPoolManip);
        auto taskGroup = taskPool.find(best.id_material);
        for (auto &task : taskGroup->second) {
            ProgtestSolver(task.orderList.get()->m_List, best.createGetPriceList());
            task.customer.get()->Completed(task.orderList);
        }
        taskPool.erase(taskGroup);
        if (taskPool.empty()) // signal that the TaskPool is empty
            cv_priceListPoolEmpty.notify_one();
        lock_TaskPool.unlock();
    }
    lock_BestPriceListRecord.unlock();

}

//-------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__

int main() {
    using namespace std::placeholders;
    CWeldingCompany test;

    AProducer p1 = make_shared<CProducerSync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
    AProducerAsync p2 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
    test.AddProducer(p1);
    test.AddProducer(p2);
    test.AddCustomer(make_shared<CCustomerTest>(2));
    p2->Start();
    test.Start(3);
    test.Stop();
    p2->Stop();
    return 0;
}

#endif /* __PROGTEST__ */
