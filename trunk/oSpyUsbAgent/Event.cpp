//
// Copyright (c) 2007 Ole Andr� Vadla Ravn�s <oleavr@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "Event.h"

#include <ntstrsafe.h>

namespace oSpy {

void
Node::Initialize ()
{
  m_name = NULL;

  m_fieldCapacity = 0;
  m_fieldKeys = NULL;
  m_fieldValues = NULL;

  m_contentIsRaw = false;
  m_contentSize = 0;
  m_content = NULL;

  m_childCapacity = 0;
  m_children = NULL;
}

int
Node::GetFieldCount () const
{
  int count = 0;

  for (int i = 0; i < m_fieldCapacity; i++)
  {
    if (m_fieldKeys[i] != NULL)
      count ++;
    else
      break;
  }

  return count;
}

int
Node::GetChildCount () const
{
  int count = 0;

  for (int i = 0; i < m_childCapacity; i++)
  {
    if (m_children[i] != NULL)
      count ++;
    else
      break;
  }

  return count;
}

void
Node::AppendChild (Node * node)
{
  int slotIndex = GetChildCount ();
  if (slotIndex == m_childCapacity)
  {
    KdPrint (("Not enough elements reserved for AppendChild"));
    return;
  }

  m_children[slotIndex] = node;
}

void
Event::Initialize (ULONG id,
                   LARGE_INTEGER timestamp,
                   const char * eventType,
                   int childCapacity)
{
  Node::Initialize ();

  m_userData = NULL;

  m_offset = 0;

  for (int i = 0; i < EVENT_NUM_BULK_SLOTS; i++)
    m_bulkStorage[i] = NULL;

  m_name = CreateString ("Event");

  CreateFieldStorage (this, 6);
  AddFieldToNodePrintf (this, "id", "%lu", id);
  AddFieldToNodePrintf (this, "timestamp", "%lld", timestamp.QuadPart);
  AddFieldToNode (this, "type", eventType);
  AddFieldToNode (this, "processName", "oSpyUsbAgent.sys");
  AddFieldToNode (this, "processId", "0");
  AddFieldToNode (this, "threadId", "0");

  CreateChildStorage (this, childCapacity);
}

void
Event::Destroy ()
{
  for (int i = 0; i < EVENT_NUM_BULK_SLOTS; i++)
  {
    if (m_bulkStorage[i] != NULL)
    {
      ExFreePool (m_bulkStorage[i]);
      m_bulkStorage[i] = NULL;
    }
  }
}

Node *
Event::CreateElement (const char * name,
                      int fieldCapacity,
                      int childCapacity)
{
  Node * node = static_cast <Node *> (ReserveStorage (sizeof (Node)));

  node->Initialize ();
  node->m_name = CreateString (name);
  CreateFieldStorage (node, fieldCapacity);
  CreateChildStorage (node, childCapacity);

  return node;
}

Node *
Event::CreateTextNode (const char * name,
                       int fieldCapacity,
                       const char * content, ...)
{
  Node * node = static_cast <Node *> (ReserveStorage (sizeof (Node)));
  node->Initialize ();

  node->m_name = CreateString (name);

  CreateFieldStorage (node, fieldCapacity);

  va_list argList;
  va_start (argList, content);

  char buf[1024];
  NTSTATUS status;
  status = RtlStringCbVPrintfA (buf, sizeof (buf), content, argList);
  if (NT_SUCCESS (status))
  {
    node->m_content = reinterpret_cast <UCHAR *> (CreateString (buf));
    node->m_contentSize = static_cast <int> (strlen (buf));
  }
  else
  {
    KdPrint (("RtlStringCbVPrintfA failed"));
  }

  return node;
}

Node *
Event::CreateDataNode (const char * name,
                       int fieldCapacity,
                       const void * data,
                       int dataSize)
{
  Node * node = static_cast <Node *> (ReserveStorage (sizeof (Node)));
  node->Initialize ();

  node->m_name = CreateString (name);

  CreateFieldStorage (node, fieldCapacity);

  node->m_contentIsRaw = true;
  node->m_content = static_cast <UCHAR *> (ReserveStorage (dataSize));
  memcpy (node->m_content, data, dataSize);
  node->m_contentSize = dataSize;

  return node;
}

void *
Event::ReserveStorage (int size)
{
  if (static_cast <int> (sizeof (m_storage)) - m_offset < size)
  {
    KdPrint (("ReserveStorage: not enough space, allocating bulk slot"));

    for (int i = 0; i < EVENT_NUM_BULK_SLOTS; i++)
    {
      if (m_bulkStorage[i] != NULL)
      {
        m_bulkStorage[i] = ExAllocatePool (NonPagedPool, size);
        return m_bulkStorage[i];
      }
    }

    KdPrint (("ReserveStorage: no bulk-slots left, should never happen"));
    return ExAllocatePool (NonPagedPool, size);
  }

  void * result = m_storage + m_offset;
  m_offset += size;
  return result;
}

char *
Event::CreateString (const char * str)
{
  int storageNeeded = static_cast <ULONG> (strlen (str)) + 1;

  char * result = static_cast <char *> (ReserveStorage (storageNeeded));
  RtlStringCbCopyA (result, storageNeeded, str);
  return result;
}

void
Event::CreateFieldStorage (Node * node, int fieldCapacity)
{
  node->m_fieldCapacity = fieldCapacity;

  int size = sizeof (char *) * fieldCapacity;
  if (size > 0)
  {
    node->m_fieldKeys = static_cast <char **> (
      ReserveStorage (size));
    memset (node->m_fieldKeys, 0, size);

    node->m_fieldValues = static_cast <char **> (
      ReserveStorage (size));
    memset (node->m_fieldValues, 0, size);
  }
  else
  {
    node->m_fieldKeys = NULL;
    node->m_fieldValues = NULL;
  }
}

void
Event::AddFieldToNode (Node * node,
                       const char * key,
                       const char * value)
{
  int slotIndex = node->GetFieldCount ();
  if (slotIndex == node->m_fieldCapacity)
  {
    KdPrint (("Not enough fields reserved for AppendFieldRaw"));
    return;
  }

  node->m_fieldKeys[slotIndex] = CreateString (key);
  node->m_fieldValues[slotIndex] = CreateString (value);
}

void
Event::AddFieldToNodePrintf (Node * node,
                             const char * key,
                             const char * valueFormat, ...)
{
  va_list argList;
  va_start (argList, valueFormat);

  char buf[1024];
  NTSTATUS status;
  status = RtlStringCbVPrintfA (buf, sizeof (buf), valueFormat, argList);
  if (!NT_SUCCESS (status)) return;

  AddFieldToNode (node, key, buf);
}

void
Event::CreateChildStorage (Node * node, int childCapacity)
{
  node->m_childCapacity = childCapacity;

  int size = sizeof (Node *) * childCapacity;

  node->m_children = static_cast <Node **> (ReserveStorage (size));
  memset (node->m_children, 0, size);
}

} // namespace oSpy
