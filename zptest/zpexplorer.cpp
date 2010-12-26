#include "zpexplorer.h"
#include "zpack.h"
#include <cassert>
#include <algorithm>
#include "fileenum.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
ZpExplorer::ZpExplorer()
	: m_pack(NULL)
{
	m_root.isDirectory = true;
	m_root.parent = NULL;
	m_currentNode = &m_root;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
ZpExplorer::~ZpExplorer()
{
	if (m_pack != NULL)
	{
		zp::close(m_pack);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool ZpExplorer::open(const std::string& path)
{
	clear();
	m_pack = zp::open(path.c_str(), 0);
	if (m_pack == NULL)
	{
		return false;
	}
	unsigned long count = m_pack->getFileCount();
	for (unsigned long i = 0; i < count; ++i)
	{
		char buffer[256];
		m_pack->getFilenameByIndex(buffer, sizeof(buffer), i);
		std::string filename = buffer;
		insertFileToTree(filename);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool ZpExplorer::create(const std::string& path, const std::string& inputPath)
{
	clear();
	m_pack = zp::create(path.c_str());
	if (m_pack == NULL)
	{
		return false;
	}
	if (!inputPath.empty())
	{
		g_basePath = inputPath;
		if (g_basePath.back() != '/')
		{
			g_basePath += "/";
		}
		enumFile(g_basePath, addPackFile, this);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ZpExplorer::close()
{
	clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool ZpExplorer::isOpen()
{
	return (m_pack != NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
zp::IPackage* ZpExplorer::getPack()
{
	return m_pack;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
const std::string& ZpExplorer::getPath()
{
	return m_currentPath;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ZpExplorer::enterRoot()
{
	m_currentNode = &m_root;
	updatePath();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ZpExplorer::enter(const std::string& dir)
{
	assert(m_currentNode != NULL);
	ZpNode* child = findChild(m_currentNode, dir, FIND_DIR);
	if (child != NULL)
	{
		m_currentNode = child;
		updatePath();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ZpExplorer::exit()
{
	if (m_currentNode->parent != NULL)
	{
		m_currentNode = m_currentNode->parent;
		updatePath();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool ZpExplorer::add(const std::string& filename)
{
	if (m_pack == NULL || filename.empty())
	{
		return false;
	}
	g_basePath.clear();
	size_t pos = filename.rfind('/');

	WIN32_FIND_DATAA fd;
	HANDLE findFile = ::FindFirstFileA(filename.c_str(), &fd);
	if (findFile != INVALID_HANDLE_VALUE && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
	{
		//it's a file
		std::string nakedFilename = filename.substr(pos + 1, filename.length() - pos - 1);
		return addFile(filename, nakedFilename);
	}
	//it's a directory
	if (pos != std::string::npos)
	{
		//dir
		g_basePath = filename.substr(0, pos + 1);
	}
	if (filename.back() != '/')
	{
		enumFile(filename + "/", addPackFile, this);
	}
	else
	{
		enumFile(filename, addPackFile, this);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool ZpExplorer::remove(const std::string& filename)
{
	std::string archivedName = m_currentPath + filename;
	std::list<ZpNode>::iterator found;
	ZpNode* child = findChild(m_currentNode, filename, FIND_ANY, &found);
	if (child != NULL)
	{
		removeChildRecursively(child, m_currentPath);
		m_currentNode->children.erase(found);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
const ZpNode* ZpExplorer::getNode()
{
	return m_currentNode;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ZpExplorer::clear()
{
	m_root.children.clear();
	m_currentNode = &m_root;
	updatePath();
	if (m_pack != NULL)
	{
		zp::close(m_pack);
		m_pack = NULL;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool ZpExplorer::addFile(const std::string& filename, const std::string& relativePath)
{
	std::string archivedName = m_currentPath + relativePath;
	if (!m_pack->addFile(filename.c_str(), archivedName.c_str()))
	{
		return false;
	}
	insertFileToTree(relativePath);
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ZpExplorer::removeChildRecursively(ZpNode* node, const std::string& path)
{
	assert(node != NULL && m_pack != NULL);
	std::string currentPath = path + node->name;
	if (!node->isDirectory)
	{
		m_pack->removeFile(currentPath.c_str());
		return;
	}
	currentPath += "/";
	for (std::list<ZpNode>::iterator iter = node->children.begin();
		iter != node->children.end();
		++iter)
	{
		removeChildRecursively(&(*iter), currentPath);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ZpExplorer::insertFileToTree(const std::string& filename)
{
	assert(m_currentNode == &m_root);
	ZpNode* node = m_currentNode;
	std::string filenameLeft = filename;
	while (filenameLeft.length() > 0)
	{
		size_t pos = filenameLeft.find_first_of("/");
		if (pos == std::string::npos)
		{
			ZpNode newNode;
			newNode.parent = node;
			newNode.isDirectory = false;
			newNode.name = filenameLeft;
			node->children.push_back(newNode);
			return;
		}
		std::string dirName = filenameLeft.substr(0, pos);
		filenameLeft = filenameLeft.substr(pos + 1, filenameLeft.length() - pos - 1);
		ZpNode* child = findChild(node, dirName, FIND_DIR);
		if (child != NULL)
		{
			node = child;
		}
		else
		{
			ZpNode newNode;
			newNode.isDirectory = true;
			newNode.parent = node;
			newNode.name = dirName;
			node->children.push_back(newNode);
			node = &node->children.back();
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
ZpNode* ZpExplorer::findChild(ZpNode* node, const std::string& name, FindType type, std::list<ZpNode>::iterator* returnIter)
{
	assert(node != NULL);
	std::string lowerName = name;
	std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
	for (std::list<ZpNode>::iterator iter = node->children.begin();
		iter != node->children.end();
		++iter)
	{
		if ((type == FIND_DIR && !iter->isDirectory) || (type == FIND_FILE && iter->isDirectory))
		{
			continue;
		}
		std::string dirName = iter->name;
		std::transform(dirName.begin(), dirName.end(), dirName.begin(), ::tolower);
		if (dirName == lowerName)
		{
			if (returnIter != NULL)
			{
				*returnIter = iter;
			}
			return &(*iter);
		}
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ZpExplorer::updatePath()
{
	m_currentPath.clear();
	ZpNode* node = m_currentNode;
	while (node != &m_root)
	{
		m_currentPath = node->name + "/" + m_currentPath;
		node = node->parent;
	}
}
