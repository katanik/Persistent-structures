#include "persistent_container.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <initializer_list>
#include <memory>
#include <vector>

namespace
{

	template<typename T>
	class PersistentListInvalidator;

	template<typename T>
	class ListNode;

	template<typename T>
	struct NodeVersion
	{
		NodeVersion()
		{
			m_version = -1;
			m_value = T{};
			m_pLeft = m_pRight = nullptr;
		}

		NodeVersion(const T& value, int version)
		{
			m_version = version;
			m_value = value;
			m_pLeft = m_pRight = nullptr;
		}

		int m_version;
		T m_value;
		std::shared_ptr<ListNode<T> > m_pLeft;
		std::shared_ptr<ListNode<T> > m_pRight;
	};

	template<typename T>
	class ListNode
	{
	public:
		ListNode() = default;

		ListNode(const T& value, int version) :
			m_first(value, version),
			m_second()
		{}

		bool isFull()
		{
			return m_isFull;
		}

		void initSecond(const T& value, int version)
		{
			m_second = m_first;
			m_second.m_version = version;
			m_second.m_value = value;
			m_isFull = true;
		}

		std::shared_ptr<ListNode<T> > getLeft(int version)
		{
			assert(version >= m_first.m_version);
			if (version < m_first.m_version)
				return nullptr;

			if (m_isFull && m_second.m_version <= version)
				return m_second.m_pLeft;
			else
				return m_first.m_pLeft;
		}

		std::shared_ptr<ListNode<T> > getRight(int version)
		{
			assert(version >= m_first.m_version);
			if (version < m_first.m_version)
				return nullptr;

			if (m_isFull && m_second.m_version <= version)
				return m_second.m_pRight;
			else
				return m_first.m_pRight;
		}

		void setLeft(std::shared_ptr<ListNode<T> > pLeft, bool isFirst = true)
		{
			if (isFirst)
				m_first.m_pLeft = pLeft;
			else
				m_second.m_pLeft = pLeft;
		}

		void setRight(std::shared_ptr<ListNode<T> > pRight, bool isFirst = true)
		{
			if (isFirst)
				m_first.m_pRight = pRight;
			else
				m_second.m_pRight = pRight;
		}

		void setVal(const T& value, int version)
		{
			assert(version >= m_first.m_version);
			if (version < m_first.m_version)
				return;

			if (m_isFull && m_second.m_version <= version)
				m_second.m_value = value;
			else
				m_first.m_value = value;
		}

		T getVal(int version)
		{
			assert(version >= m_first.m_version);
			if (version < m_first.m_version)
				throw std::exception();

			if (m_isFull && m_second.m_version <= version)
				return m_second.m_value;
			else
				return m_first.m_value;
		}

		bool clear(int version)
		{
			if (m_isFull)
			{
				if (version >= m_second.m_version)
					return false;

				m_second.m_pLeft.reset();
				m_second.m_pRight.reset();
				return true;
			}
			else
			{
				if (version >= m_first.m_version)
					return false;

				m_first.m_pLeft.reset();
				m_first.m_pRight.reset();
				return true;
			}
		}

		std::pair<int, int> versions()
		{
			return std::make_pair(m_first.m_version, m_second.m_version);
		}

	private:
		bool m_isFull = false;
		NodeVersion<T> m_first, m_second;
	};

	template<typename T>
	class PersistentListInvalidator
	{
	public:
		using NodePtr = std::shared_ptr<ListNode<T> >;

		PersistentListInvalidator(std::vector<NodePtr>& apHeads, std::vector<NodePtr>& apTails) :
			m_apHeads(apHeads),
			m_apTails(apTails)
		{}

		void add(const NodePtr& pNode)
		{
			m_apNodes.push_back(pNode);
		}

		void addHead(const NodePtr& pNode)
		{
			m_apHeads.push_back(pNode);
		}

		void addTail(const NodePtr& pNode)
		{
			m_apTails.push_back(pNode);
		}

		void updateLastHead(int version)
		{
			if (m_apHeads.back()->getLeft(version) != nullptr)
			{
				m_apHeads.back() = m_apHeads.back()->getLeft(version);
			}
		}

		void invalidate(int version)
		{
			while (!m_apNodes.empty() && m_apNodes.back()->clear(version))
			{
				m_apNodes.pop_back();
			}
			while (!m_apHeads.empty() && m_apHeads.back()->versions().first > version)
			{
				m_apHeads.pop_back();
			}
		}

	private:
		std::vector<NodePtr>& m_apHeads;
		std::vector<NodePtr>& m_apTails;
		std::vector<NodePtr> m_apNodes;
	};


}

template<typename T>
class PersistentList;

template<typename T>
class PersistentListIterator
{
public:
	/*
	* Sets the iterator to the next position
	*/
	void next()
	{
		assert(m_pItem != nullptr);
		if (m_pItem == nullptr)
			throw std::exception();

		m_pItem = m_pItem->getRight(m_version);
	}

	/*
	* Sets the iterator to the previous position
	*/
	void prev()
	{
		assert(m_pItem != nullptr);
		if (m_pItem == nullptr)
			throw std::exception();

		m_pItem = m_pItem->getLeft(m_version);
	}

	/*
	* Checks if iterator is at the end, throws exception if invalid
	* @return false, if at the end, true otherwise
	*/
	bool done()
	{
		assert(m_pItem != nullptr);
		if (m_pItem == nullptr)
			throw std::exception();

		return m_pItem->getRight(m_version) == nullptr;
	}

	/**
	* Sets value to the element which iterator points to
	* @param value
	*/
	void setVal(const T& val)
	{
		assert(m_pItem != nullptr);
		if (m_pItem == nullptr)
			throw std::exception();

		m_pInvalidator->invalidate(m_version);

		if (!m_pItem->isFull())
		{
			m_pItem->initSecond(val, m_version + 1);
			m_pInvalidator->add(m_pItem);
		}
		else
		{
			auto pNode = std::make_shared<ListNode<T> >(val, m_version + 1);
			auto pPrev = pNode;

			if (m_pItem->getLeft(m_version) == nullptr)
			{
				m_pInvalidator->addHead(pNode);
			}

			copyLeft(m_pItem->getLeft(m_version), val, pPrev);

			pPrev = pNode;
			copyRight(m_pItem->getRight(m_version), val, pPrev);
		}

		m_pInvalidator->updateLastHead(m_version + 1);

		m_lastVersion = ++m_version;
	}

	/**
	* Gets value of the element which iterator points to
	* @param value
	*/
	T getVal()
	{
		assert(m_pItem != nullptr && m_pItem->getRight(m_version) != nullptr);
		if (m_pItem == nullptr || m_pItem->getRight(m_version) == nullptr)
			throw std::exception();

		return m_pItem->getVal(m_version);
	}

private:
	using NodePtr = std::shared_ptr<ListNode<T> >;
	friend class PersistentList<T>;

	PersistentListIterator(const NodePtr& pNode, int& version, int& lastVer, std::shared_ptr<PersistentListInvalidator<T> > pInvalidator) :
		m_version(version),
		m_lastVersion(lastVer),
		m_pInvalidator(pInvalidator)
	{
		m_pItem = pNode;
	}

	void copyLeft(const NodePtr& pFirst, const T& val, NodePtr& pPrev)
	{
		for (auto pLeft = pFirst; pLeft != nullptr; pLeft = pLeft->getLeft(m_version))
		{
			if (pLeft->isFull())
			{
				auto pCopy = std::make_shared<ListNode<T> >(pLeft->getVal(m_version), m_version + 1);
				pPrev->setLeft(pCopy);
				pCopy->setRight(pPrev);
				m_pInvalidator->add(pCopy);

				if (pLeft->getLeft(m_version) == nullptr)
				{
					m_pInvalidator->addHead(pCopy);
				}
				pPrev = pCopy;
			}
			else
			{
				pLeft->initSecond(val, m_version + 1);
				pLeft->setRight(pPrev, false);
				pPrev->setLeft(pLeft);
				m_pInvalidator->add(pLeft);
				break;
			}
		}
	}

	void copyRight(const NodePtr& pFirst, const T& val, NodePtr& pPrev)
	{
		for (auto pRight = pFirst; pRight != nullptr; pRight = pRight->getRight(m_version))
		{
			if (pRight->isFull())
			{
				auto pCopy = std::make_shared<ListNode<T> >(pRight->getVal(m_version), m_version + 1);
				pPrev->setRight(pCopy);
				pCopy->setLeft(pPrev);
				m_pInvalidator->add(pCopy);
				pPrev = pCopy;

				if (pRight->getRight(m_version) == nullptr)
				{
					m_pInvalidator->addTail(pCopy);
				}
			}
			else
			{
				pRight->initSecond(val, m_version + 1);
				pRight->setLeft(pPrev, false);
				pPrev->setRight(pRight);
				m_pInvalidator->add(pRight);
				break;
			}
		}
	}

	std::shared_ptr<PersistentListInvalidator <T> > m_pInvalidator;
	int &m_version, &m_lastVersion;
	NodePtr m_pItem;
};

template<typename T>
class PersistentList : public PersistentBase
{
public:
	friend class PersistentListIterator<T>;
	using PersistentListIteratorPtr = std::shared_ptr<PersistentListIterator<T> >;

	PersistentList()
	{
		m_apHeads.push_back(std::make_shared<ListNode<T> >());
		m_apTails = m_apHeads;
		m_pInvalidator = std::make_shared<PersistentListInvalidator<T> >(m_apHeads, m_apTails);
	}

	/**
	* Gets new itarator to the beginning of the list
	* @return iterator to the beginning
	*/
	PersistentListIteratorPtr begin()
	{
		int ind = 0;
		for (int i = (int)m_apHeads.size() - 1; i >= 0; i--)
		{
			if (m_apHeads[i]->versions().first <= m_version)
			{
				ind = i;
				break;
			}
		}

		auto pBegin = std::shared_ptr<PersistentListIterator<T> >(new PersistentListIterator<T>(m_apHeads[ind], m_version, m_lastVersion, m_pInvalidator));
		return pBegin;
	}

	/**
	* Gets new itarator to the end of the list
	* @return iterator to the end
	*/
	PersistentListIteratorPtr end()
	{
		int ind = 0;
		for (int i = (int)m_apTails.size() - 1; i >= 0; i--)
		{
			if (m_apTails[i]->versions().first <= m_version)
			{
				ind = i;
				break;
			}
		}

		auto pEnd = std::shared_ptr<PersistentListIterator<T> >(new PersistentListIterator<T>(m_apTails[ind], m_version, m_lastVersion, m_pInvalidator));
		return pEnd;
	}

	/**
	* Inserts new element to the position, which itarator points to, throws exception if iterator is invalid
	* @param pIter - poiner to the iterator
	* @param val - value of element
	*/
	PersistentListIteratorPtr insert(PersistentListIteratorPtr& pIter, T val)
	{
		assert(pIter != nullptr);
		if (pIter == nullptr)
			throw std::exception();

		m_pInvalidator->invalidate(m_version);

		auto pNode = std::make_shared<ListNode<T> >(val, m_version + 1);
		m_pInvalidator->add(pNode);

		if (pIter->m_pItem->getLeft(m_version) == nullptr)
		{
			m_pInvalidator->addHead(pNode);
		}

		auto pPrev = pNode;
		copyLeft(pIter->m_pItem->getLeft(m_version), pPrev);

		pPrev = pNode;
		copyRight(pIter->m_pItem, pPrev);

		m_pInvalidator->updateLastHead(m_version + 1);

		m_lastVersion = ++m_version;
		pIter = std::shared_ptr<PersistentListIterator<T> >(new PersistentListIterator<T>(pNode->getRight(m_version), m_version, m_lastVersion, m_pInvalidator));
		auto pNewIter = std::shared_ptr<PersistentListIterator<T> >(new PersistentListIterator<T>(pNode, m_version, m_lastVersion, m_pInvalidator));
		return pNewIter;
	}

	/**
	* Erases element which iterator points to, throws exception if iterator is invalid or points to the end
	* @param key
	* @return true, if element is successfully deleted
	*/
	PersistentListIteratorPtr erase(PersistentListIteratorPtr& pIter)
	{
		assert(pIter != nullptr && pIter->m_pItem->getRight(m_version) != nullptr);
		if (pIter == nullptr && pIter->m_pItem->getRight(m_version) != nullptr)
			throw std::exception();

		m_pInvalidator->invalidate(m_version);

		auto pLeftNode = pIter->m_pItem->getLeft(m_version);
		auto pRightNode = pIter->m_pItem->getRight(m_version);

		NodePtr pLeftClonedNode = nullptr, pRightClonedNode = nullptr;

		if (pLeftNode != nullptr)
		{
			if (!pLeftNode->isFull())
			{
				pLeftNode->initSecond(pLeftNode->getVal(m_version), m_version + 1);
				m_pInvalidator->add(pLeftNode);
			}
			else
			{
				pLeftClonedNode = std::make_shared<ListNode<T> >(pLeftNode->getVal(m_version), m_version + 1);
				m_pInvalidator->add(pLeftClonedNode);
				if (pLeftNode->getLeft(m_version) == nullptr)
				{
					m_pInvalidator->addHead(pLeftClonedNode);
				}

				auto pPrev = pLeftClonedNode;
				copyLeft(pLeftNode->getLeft(m_version), pPrev);
			}
		}

		if (!pRightNode->isFull())
		{
			pRightNode->initSecond(pRightNode->getVal(m_version), m_version + 1);
			m_pInvalidator->add(pRightNode);
			if (pLeftNode == nullptr)
			{
				pRightNode->setLeft(nullptr, false);
				m_pInvalidator->addHead(pRightNode);
			}
		}
		else
		{
			pRightClonedNode = std::make_shared<ListNode<T> >(pRightNode->getVal(m_version), m_version + 1);
			m_pInvalidator->add(pRightClonedNode);
			if (pLeftNode == nullptr)
			{
				m_pInvalidator->addHead(pRightClonedNode);
			}

			auto pPrev = pRightClonedNode;
			copyRight(pRightNode->getRight(m_version), pPrev);
		}

		if (pLeftNode != nullptr)
		{
			if (pLeftClonedNode == nullptr)
			{
				if (pRightClonedNode == nullptr)
				{
					pLeftNode->setRight(pRightNode, false);
					pRightNode->setLeft(pLeftNode, false);
				}
				else
				{
					pLeftNode->setRight(pRightClonedNode, false);
					pRightClonedNode->setLeft(pLeftNode);
				}
			}
			else
			{
				if (pRightClonedNode == nullptr)
				{
					pLeftClonedNode->setRight(pRightNode);
					pRightNode->setLeft(pLeftClonedNode, false);
				}
				else
				{
					pLeftClonedNode->setRight(pRightClonedNode);
					pRightClonedNode->setLeft(pLeftClonedNode);
				}
			}
		}

		m_pInvalidator->updateLastHead(m_version + 1);

		m_lastVersion = ++m_version;
		pIter.reset();

		if (pRightClonedNode != nullptr)
			return std::shared_ptr<PersistentListIterator<T> >(new PersistentListIterator<T>(pRightClonedNode, m_version, m_lastVersion, m_pInvalidator));
		else
			return std::shared_ptr<PersistentListIterator<T> >(new PersistentListIterator<T>(pRightNode, m_version, m_lastVersion, m_pInvalidator));
	}

	/**
	* Prints elements of the list
	*/
	void print()
	{
		for (auto pIter = begin(); !pIter->done(); pIter->next())
		{
			std::cout << pIter->getVal() << " ";
		}
		puts("");
	}

	/**
	* Undo last numIter operations of 'set', 'insert', 'erase' types
	* @param numIter
	*/
	void undo(int numIter = 1, bool clearHistory = false) override
	{
		m_version = std::max(0, m_version - numIter);
		if (clearHistory)
		{
			m_pInvalidator->invalidate(m_version);
		}
	}

	/**
	* Reapplies last cancelled numIter operations of 'set', 'insert', 'erase' types
	* @param numIter
	*/
	void redo(int numIter = 1)
	{
		m_version = std::min(m_lastVersion, m_version + numIter);
	}

	/*
	* Gets number of versions of the array
	* @return number of versions
	*/
	int lastVersion() override
	{
		return m_lastVersion + 1;
	}

private:
	void copyLeft(const std::shared_ptr<ListNode<T> >& pFirst, std::shared_ptr<ListNode<T> >& pPrev)
	{
		for (auto pLeft = pFirst; pLeft != nullptr; pLeft = pLeft->getLeft(m_version))
		{
			if (pLeft->isFull())
			{
				auto pCopy = std::make_shared<ListNode<T> >(pLeft->getVal(m_version), m_version + 1);
				pPrev->setLeft(pCopy);
				pCopy->setRight(pPrev);
				m_pInvalidator->add(pCopy);

				if (pLeft->getLeft(m_version) == nullptr)
				{
					m_pInvalidator->addHead(pCopy);
				}
				pPrev = pCopy;
			}
			else
			{
				pLeft->initSecond(pLeft->getVal(m_version), m_version + 1);
				pLeft->setRight(pPrev, false);
				pPrev->setLeft(pLeft);
				m_pInvalidator->add(pLeft);
				break;
			}
		}
	}

	void copyRight(const std::shared_ptr<ListNode<T> >& pFirst, std::shared_ptr<ListNode<T> >& pPrev)
	{
		for (auto pRight = pFirst; pRight != nullptr; pRight = pRight->getRight(m_version))
		{
			if (pRight->isFull())
			{
				auto pCopy = std::make_shared<ListNode<T> >(pRight->getVal(m_version), m_version + 1);
				pPrev->setRight(pCopy);
				pCopy->setLeft(pPrev);
				m_pInvalidator->add(pCopy);
				pPrev = pCopy;

				if (pRight->getRight(m_version) == nullptr)
				{
					m_pInvalidator->addTail(pCopy);
				}
			}
			else
			{
				pRight->initSecond(pRight->getVal(m_version), m_version + 1);
				pRight->setLeft(pPrev, false);
				pPrev->setRight(pRight);
				m_pInvalidator->add(pRight);
				break;
			}
		}
	}

private:
	using NodePtr = std::shared_ptr<ListNode<T> >;

	int m_version = 0, m_lastVersion = 0;
	std::vector<NodePtr> m_apHeads;
	std::vector<NodePtr> m_apTails;
	std::shared_ptr<PersistentListInvalidator<T> > m_pInvalidator;
};
