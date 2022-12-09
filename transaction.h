#include "persistent_container.h"

#include <vector>

class Transaction
{
public:
	template<typename... ArgPtrs>
	Transaction(ArgPtrs... pArgs)
	{
		init(pArgs...);
	}

	void addContainer(PersistentBasePtr pContainer)
	{
		if (pContainer != nullptr)
		{
			m_apContainers.push_back(pContainer);
			m_versions.push_back(pContainer->lastVersion());
		}
	}

	template<typename Function, typename... Args>
	bool run(Function function, Args... args)
	{
		try
		{
			function(args...);
		}
		catch (...)
		{
			m_isSucceeded = false;
			return false;
		}

		return true;
	}

	~Transaction()
	{
		if (!m_isSucceeded)
		{
			for (int i = 0; i < (int)m_versions.size(); i++)
			{
				m_apContainers[i]->undo(m_apContainers[i]->lastVersion() - m_versions[i], true);

			}
		}
	}
private:
	void init() {}

	template<typename... ArgPtrs>
	void init(PersistentBasePtr pContainer, ArgPtrs... pArgs)
	{
		if (pContainer != nullptr)
		{
			m_apContainers.push_back(pContainer);
			m_versions.push_back(pContainer->lastVersion());
		}
		init(pArgs...);
	}

	std::vector<PersistentBasePtr> m_apContainers;
	std::vector<int> m_versions;
	bool m_isSucceeded = true;
};

