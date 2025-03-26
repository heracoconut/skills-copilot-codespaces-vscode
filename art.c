/* A highly compressed radix tree
 *  Based on the Adaptive Radix Tree by 
 *    Viktor Leis, Alfons Kemper and Thomas Neumann
 *  
 * Author  - Matthew Levenstein
 * License - MIT
 */
 头文件 
#ifndef _ART_H
#define _ART_H

typedef unsigned char byte_t;
typedef unsigned long word_t;

#define _LEAF     0
#define _SINGLE   1
#define _LINEAR   4
#define _INNER    5
#define _LINEAR16 16
#define _SPAN     48
#define _RADIX    255

typedef struct {
  byte_t type; 
  byte_t plen; 
  byte_t path[sizeof(word_t)];
  short  rcnt;
} artNodeHeader;

typedef struct {
  word_t  val;
  byte_t* key;
  void*   next;
} artVal;

typedef struct {
  artNodeHeader head;
} artNode;

typedef struct {
  artNode* root;
} Art;

typedef struct {
  artNodeHeader  head;
  word_t         val;
} artNodeLeaf;

typedef struct {
  artNodeHeader  head;
  byte_t         map;
  word_t         radix;
  word_t         val;
} artNodeSingle;

typedef struct {
  artNodeHeader  head;
  byte_t         map[_LINEAR];
  word_t         radix[_LINEAR];
} artNodeInner;

typedef struct {
  artNodeHeader  head;
  byte_t         map[_LINEAR];
  word_t         radix[_LINEAR];
  word_t         val;
} artNodeLinear;

typedef struct {
  artNodeHeader  head;
  byte_t         map[_LINEAR16];
  word_t         radix[_LINEAR16];
  word_t         val;
} artNodeLinear16;

typedef struct {
  artNodeHeader  head;
  byte_t         map[256];
  word_t         radix[_SPAN];
  word_t         val;
} artNodeSpan;

typedef struct {
  artNodeHeader  head;
  word_t         val;
  word_t         radix[256];
} artNodeRadix;

/* API */
void      artPut                   (Art*, byte_t*, int, word_t);
word_t    artGet                   (Art*, byte_t*, int);
int       artRemove                (Art*, byte_t*, int);
Art*      artNew                   (void);
artVal*   artGetWithPrefix         (Art*, byte_t*, int);

#endif
源文件 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "art.h"

word_t bytes = 0;

void*     artMalloc                (size_t);
void      artNodeAddChild          (artNode**, artNode*, byte_t);
void      artNodeReplaceChild      (artNode*, artNode*, byte_t);
artNode*  artNodeGetChild          (artNode*, byte_t);
void      artNodeRemoveChild       (artNode**, byte_t);
void      artNodeResize            (artNode**, int);
void*     artNodeAlloc             (int);
void      artNodeFree              (artNode*);
void      artWordToArray           (byte_t*, word_t); 
word_t    artArrayToWord           (byte_t*);
void      artNodeMovePrefix        (artNode*, int);
byte_t    artNodePrefixIdx         (artNode*, int);
int       artNodeCheckPrefix       (artNode*, byte_t*, int, int);
void      artNodeSetPrefix         (artNode*, byte_t*, int);
void      artNodeMergeWithChild    (artNode**);
byte_t*   artNodeGetPrefix         (artNode*); 
void      artNodeSetVal            (artNode**, word_t);
void      artNodeCopyPrefix        (artNode*, artNode*);
word_t    artNodeGetVal            (artNode*);
artNode*  artGetNode               (Art*, byte_t*, int, int); 
void      __artGetWithPrefix       (artNode*, artVal*, byte_t*, int);

void artNodePrintDetails (artNode*);

void* artMalloc (size_t size) {
  void* buf = malloc(size);
  word_t b = (word_t)buf;
  if (!buf) {
    fprintf(stderr, "Fatal: out of memory.");
    abort();
  }
  memset(buf, 0, size);
  return buf;
}

void artWordToArray (byte_t* b, word_t w) {
  int idx, i, s = (sizeof(word_t) * 8);
  for (i = 8; i <= s; i += 8) {
    idx = (i/8) - 1;
    b[idx] = (w >> (s - i)) & 0xff;
  }
}

word_t artArrayToWord (byte_t* b) {
  int idx, i, s = (sizeof(word_t) * 8);
  word_t w = 0, n;
  for (i = 8; i <= s; i += 8) {
    idx = (i/8) - 1;
    n = (word_t)b[idx];
    w |= (n << (s - i));
  }
  return w;
}

int artNodeCheckPrefix (artNode* n, byte_t* k, int l, int d) {
  int i, j = 0;
  byte_t* k0;
  if (n->head.plen > sizeof(word_t))
    k0 = (byte_t *)artArrayToWord(n->head.path);
  else k0 = n->head.path;
  for (i = d; j < n->head.plen && i < l; i++, j++) {
    if (k0[j] != k[i]) break;
  }
  return j;
}

byte_t artNodePrefixIdx (artNode* n, int idx) {
  byte_t* k0;
  if (n->head.plen > sizeof(word_t))
    k0 = (byte_t *)artArrayToWord(n->head.path);
  else k0 = n->head.path;
  return k0[idx];
}

void artNodeSetPrefix (artNode* n, byte_t* p, int l) {
  byte_t* pref;
  int s = sizeof(word_t);
  if (n->head.plen > s) {
    free((void *)artArrayToWord(n->head.path));
  }
  memset(n->head.path, 0, s);
  n->head.plen = l;
  if (l > s) {
    pref = artMalloc(l);
    memcpy(pref, p, l);
    artWordToArray(n->head.path, (word_t)pref);
  } else {
    memcpy(n->head.path, p, l);
  }
}

void artNodeMovePrefix (artNode* n, int i) {
  int f, s = sizeof(word_t);
  byte_t *p, *np, l = n->head.plen;
  f = l - i;
  if (l > s) {
    p = (byte_t *)artArrayToWord(n->head.path);
    if (f > s) {
      np = artMalloc(f);
      memcpy(np, p + i, f);
      artWordToArray(n->head.path, (word_t)np);
      free(p);
    } else {
      memcpy(n->head.path, p + i, f);
      free(p);
    }
  } else {
    memcpy(n->head.path, n->head.path + i, f);
  }
  n->head.plen = f;
}

byte_t* artNodeGetPrefix (artNode* n) {
  int s = sizeof(word_t);
  byte_t *p, l = n->head.plen;
  int i;
  if (l > s) p = (byte_t *)artArrayToWord(n->head.path);
  else p = n->head.path;
  return p;
}

void artNodeMergeWithChild (artNode** n0) {
  artNodeSingle *pck;
  artNode* n1;
  byte_t* p;
  int l;

  if ((*n0)->head.type != _SINGLE)
    return;

  pck = (artNodeSingle *)*n0;
  n1 = (artNode *)pck->radix;
  l = pck->head.plen + n1->head.plen;
  p = artMalloc(l);
  memcpy(p, artNodeGetPrefix(*n0), pck->head.plen);
  memcpy(p + pck->head.plen, artNodeGetPrefix(n1), n1->head.plen);
  artNodeSetPrefix(n1, p, l);
  artNodeFree(*n0);
  *n0 = n1;
}

void artNodeCopyPrefix (artNode* n0, artNode* n1) {
  artNodeSetPrefix(n0, artNodeGetPrefix(n1), n1->head.plen);
}

void* artNodeAlloc (int type) {
  artNode* d;
  size_t s = 0;
  switch (type) {
    case _LEAF:  
      s = sizeof(artNodeLeaf);
    break;
    case _SINGLE:  
      s = sizeof(artNodeSingle);
    break;
    case _INNER:
      s = sizeof(artNodeInner);
    break;
    case _LINEAR:
      s = sizeof(artNodeLinear);
    break;
    case _LINEAR16: 
      s = sizeof(artNodeLinear16);
    break;
    case _SPAN: 
      s = sizeof(artNodeSpan);
    break;
    case _RADIX:
      s = sizeof(artNodeRadix);
    break;
    default: break;
  }
  bytes += s;
  d = (artNode *)artMalloc(s);
  d->head.type = type;
  return (void *)d;
}

void artNodeFree (artNode* n) {
  if (n->head.plen > sizeof(word_t)) {
    free((void *)artArrayToWord(n->head.path));
  }

  switch (n->head.type) {
    case _LEAF:
      bytes -= sizeof(artNodeLeaf);
      free((artNodeLeaf *)n);
    break;
    case _SINGLE:
      bytes -= sizeof(artNodeSingle);
      free((artNodeSingle *)n);
    break;
    case _INNER:
      bytes -= sizeof(artNodeInner);
      free((artNodeInner *)n);
    break;
    case _LINEAR:
      bytes -= sizeof(artNodeLinear);
      free((artNodeLinear *)n);
    break;
    case _LINEAR16:
      bytes -= sizeof(artNodeLinear16);
      free((artNodeLinear16 *)n);
    break;
    case _SPAN:
      bytes -= sizeof(artNodeSpan);
      free((artNodeSpan *)n);
    break;
    case _RADIX:
      bytes -= sizeof(artNodeRadix);
      free((artNodeRadix *)n);
    break;
    default:  break;
  }
}

artNode* artNodeGetChild (artNode* n, byte_t b) {
  artNodeSingle* p;
  artNodeLinear* l;
  artNodeLinear16* l16;
  artNodeSpan* s;
  artNodeRadix* r;
  artNode* ret;
  byte_t type;
  int i;

  ret = NULL;

  switch (n->head.type) {
  case _SINGLE:
    p = (artNodeSingle *)n;
    if (p->map == b) {
      ret = (artNode *)p->radix;
    }
  break;
  case _INNER:
  case _LINEAR:
    l = (artNodeLinear *)n;
    for (i = 0; i < _LINEAR; i++) {
      if (l->map[i] == b) {
        ret = (artNode *)l->radix[i];
        break;
      }
    }
  break;
  case _LINEAR16:
    l16 = (artNodeLinear16 *)n;
    for (i = 0; i < _LINEAR16; i++) {
      if (l16->map[i] == b) {
        ret = (artNode *)l16->radix[i];
        break;
      }
    }
  break;
  case _SPAN: 
    s = (artNodeSpan *)n;
    if (s->map[b] != _SPAN) {
      ret = (artNode *)s->radix[s->map[b]];
    }
  break;
  case _RADIX: 
    r = (artNodeRadix *)n;
    ret = (artNode *)r->radix[b];
  break;
  default: break;
  }

  return ret;
}

void artNodeResize (artNode** n, int grow) {
  artNodeSingle* p;
  artNodeLinear* l;
  artNodeInner* in;
  artNodeLinear16* l16;
  artNodeSpan* s;
  artNodeRadix* r;
  artNodeLeaf* k;
  byte_t type;

  type = (*n)->head.type;

  switch (type) {
  case _LEAF:
    k = (artNodeLeaf *)*n;
    p = (artNodeSingle *)artNodeAlloc(_SINGLE);
    p->head.type = _SINGLE;
    p->val = k->val;
    artNodeCopyPrefix((artNode *)p, *n);
    artNodeFree(*n);
    *n = (artNode *)p;
  break;
  case _SINGLE:
    p = (artNodeSingle *)*n;
    if (grow && p->val) {
      l = (artNodeLinear *)artNodeAlloc(_LINEAR);
      l->head.type = _LINEAR;
      l->map[0] = p->map;
      l->radix[0] = p->radix;
      l->val = p->val;
      l->head.rcnt = p->head.rcnt;
      artNodeCopyPrefix((artNode *)l, *n);
      artNodeFree(*n);
      *n = (artNode *)l;
    } else if (grow) {
      in = (artNodeInner *)artNodeAlloc(_INNER);
      in->head.type = _INNER;
      in->map[0] = p->map;
      in->radix[0] = p->radix;
      in->head.rcnt = p->head.rcnt;
      artNodeCopyPrefix((artNode *)in, *n);
      artNodeFree(*n);
      *n = (artNode *)in;
    } else {
      k = (artNodeLeaf *)artNodeAlloc(_LEAF);
      k->head.type = _LEAF;
      k->val = p->val;
      k->head.rcnt = p->head.rcnt;
      artNodeCopyPrefix((artNode *)k, *n);
      artNodeFree(*n);
      *n = (artNode *)k;
    }
  break;
  case _INNER:
    if (grow) {
      int i;
      in = (artNodeInner *)*n;
      l16 = (artNodeLinear16 *)artNodeAlloc(_LINEAR16);
      l16->head.type = _LINEAR16;
      for (i = 0; i < _LINEAR; i++) {
        l16->map[i] = in->map[i];
        l16->radix[i] = in->radix[i];
      }
      l16->val = (word_t)0;
      l16->head.rcnt = in->head.rcnt;
      artNodeCopyPrefix((artNode *)l16, *n);
      artNodeFree(*n);
      *n = (artNode *)l16;
    } else {
      int i;
      in = (artNodeInner *)*n;
      p = (artNodeSingle *)artNodeAlloc(_SINGLE);
      p->head.type = _SINGLE;
      for (i = 0; i < _LINEAR; i++) {
        if (in->radix[i]) {
          p->map = in->map[i];
          p->radix = in->radix[i];
          break;
        }
      }
      p->val = (word_t)0;
      p->head.rcnt = in->head.rcnt;
      artNodeCopyPrefix((artNode *)p, *n);
      artNodeFree(*n);
      *n = (artNode *)p;
    }
  break;
  case _LINEAR:
    if (grow) {
      int i;
      l = (artNodeLinear *)*n;
      l16 = (artNodeLinear16 *)artNodeAlloc(_LINEAR16);
      l16->head.type = _LINEAR16;
      for (i = 0; i < _LINEAR; i++) {
        l16->map[i] = l->map[i];
        l16->radix[i] = l->radix[i];
      }
      l16->val = l->val;
      l16->head.rcnt = l->head.rcnt;
      artNodeCopyPrefix((artNode *)l16, *n);
      artNodeFree(*n);
      *n = (artNode *)l16;
    } else {
      int i;
      l = (artNodeLinear *)*n;
      p = (artNodeSingle *)artNodeAlloc(_SINGLE);
      p->head.type = _SINGLE;
      for (i = 0; i < _LINEAR; i++) {
        if (l->radix[i]) {
          p->map = l->map[i];
          p->radix = l->radix[i];
          break;
        }
      }
      p->val = l->val;
      p->head.rcnt = l->head.rcnt;
      artNodeCopyPrefix((artNode *)p, *n);
      artNodeFree(*n);
      *n = (artNode *)p;
    }
  break;
  case _LINEAR16:
    l16 = (artNodeLinear16 *)*n;
    if (grow) {
      int i;
      l16 = (artNodeLinear16 *)*n;
      s = (artNodeSpan *)artNodeAlloc(_SPAN);
      s->head.type = _SPAN;
      for (i = 0; i < 256; i++) s->map[i] = _SPAN;
      for (i = 0; i < _LINEAR16; i++) {
        s->map[l16->map[i]] = i;
        s->radix[i] = l16->radix[i];
      }
      s->val = l16->val;
      s->head.rcnt = l16->head.rcnt;
      artNodeCopyPrefix((artNode *)s, *n);
      artNodeFree(*n);
      *n = (artNode *)s;
    } else if (!l16->val) {
      int i, j = 0;
      in = (artNodeInner *)artNodeAlloc(_INNER);
      in->head.type = _INNER;
      for (i = 0; i < _LINEAR16; i++) {
        if (l16->radix[i]) {
          in->map[j] = l16->map[i];
          in->radix[j++] = l16->radix[i];
        }
      }
      in->head.rcnt = l16->head.rcnt;
      artNodeCopyPrefix((artNode *)in, *n);
      artNodeFree(*n);
      *n = (artNode *)in;
    } else {
      int i, j = 0;
      l = (artNodeLinear *)artNodeAlloc(_LINEAR);
      l->head.type = _LINEAR;
      for (i = 0; i < _LINEAR16; i++) {
        if (l16->radix[i]) {
          l->map[j] = l16->map[i];
          l->radix[j++] = l16->radix[i];
        }
      }
      l->val = l16->val;
      l->head.rcnt = l16->head.rcnt;
      artNodeCopyPrefix((artNode *)l, *n);
      artNodeFree(*n);
      *n = (artNode *)l;
    }
  break;
  case _SPAN:
    if (grow) {
      int i;
      s = (artNodeSpan *)*n;
      r = (artNodeRadix *)artNodeAlloc(_RADIX);
      r->head.type = _RADIX;
      for (i = 0; i < 256; i++) {
        if (s->map[i] != _SPAN) {
          r->radix[i] = s->radix[s->map[i]];
        }
      }
      r->val = s->val;
      r->head.rcnt = s->head.rcnt;
      artNodeCopyPrefix((artNode *)r, *n);
      artNodeFree(*n);
      *n = (artNode *)r;
    } else {
      int i, j = 0;
      s = (artNodeSpan *)*n;
      l16 = (artNodeLinear16 *)artNodeAlloc(_LINEAR16);
      l16->head.type = _LINEAR16;
      for (i = 0; i < 256; i++) {
        if (s->map[i] != _SPAN) {
          l16->map[j] = (byte_t)i;
          l16->radix[j++] = s->radix[s->map[i]];
        }
      }
      l16->val = s->val;
      l16->head.rcnt = s->head.rcnt;
      artNodeCopyPrefix((artNode *)l16, *n);
      artNodeFree(*n);
      *n = (artNode *)l16;
    }
  break;
  case _RADIX: {
    int i, j = 0;
    r = (artNodeRadix *)*n;
    s = (artNodeSpan *)artNodeAlloc(_SPAN);
    s->head.type = _SPAN;
    for (i = 0; i < 256; i++) s->map[i] = _SPAN;
    for (i = 0; i < 256; i++) {
      if (r->radix[i]) {
        s->map[i] = j;
        s->radix[j++] = r->radix[i];
      }
    }
    s->val = r->val;
    s->head.rcnt = r->head.rcnt;
    artNodeCopyPrefix((artNode *)s, *n);
    artNodeFree(*n);
    *n = (artNode *)s;
  } break;
  default:  break;
  }
}

void artNodeRemoveChild (artNode** n, byte_t b) {
  artNodeSingle* p;
  artNodeLinear* l;
  artNodeLinear16* l16;
  artNodeSpan* s;
  artNodeRadix* r;
  byte_t type;
  int i, rl = -1;

  type = (*n)->head.type;

  switch (type) {
    case _SINGLE:   rl = _LEAF;     break;
    case _INNER:    rl = _SINGLE;   break;
    case _LINEAR:   rl = _SINGLE;   break;
    case _LINEAR16: rl = _LINEAR;   break;
    case _SPAN:     rl = _LINEAR16; break;
    case _RADIX:    rl = _SPAN;     break;
  }

  switch (type) {
  case _SINGLE: 
    p = (artNodeSingle *)*n;
    if (p->map == b) {
      p->radix = (word_t)0;
      p->head.rcnt = 0;
    }
  break;
  case _INNER:
  case _LINEAR:
    l = (artNodeLinear *)*n;
    for (i = 0; i < _LINEAR; i++) {
      if (l->map[i] == b && l->radix[i]) {
        l->map[i] = (byte_t)0;
        l->radix[i] = (word_t)0;
        l->head.rcnt -= 1;
        break;
      }
    }
  break;
  case _LINEAR16:
    l16 = (artNodeLinear16 *)*n;
    for (i = 0; i < _LINEAR16; i++) {
      if (l16->map[i] == b && l16->radix[i]) {
        l16->map[i] = (byte_t)0;
        l16->radix[i] = (word_t)0;
        l16->head.rcnt -= 1;
        break;
      }
    }
  break;
  case _SPAN:
    s = (artNodeSpan *)*n;
    if (s->map[b] != _SPAN) {
      s->radix[s->map[b]] = (word_t)0;
      s->map[b] = _SPAN;
      s->head.rcnt -= 1;
    }
  break;
  case _RADIX: 
    r = (artNodeRadix *)*n;
    r->radix[b] = (word_t)0;
    r->head.rcnt -= 1;
  break;
  default: break;
  }

  if (rl > -1 && (*n)->head.rcnt == rl) {
    artNodeResize(n, 0);
  }
}

void artNodeReplaceChild (artNode* n, artNode* c, byte_t b) {
  artNodeSingle* p;
  artNodeLinear* l;
  artNodeLinear16* l16;
  artNodeSpan* s;
  artNodeRadix* r;
  byte_t type;

  type = n->head.type;

  switch (type) {
  case _SINGLE:
    p = (artNodeSingle *)n;
    if (p->map == b) {
      p->radix = (word_t)c;
    }
  break;
  case _INNER:
  case _LINEAR: {
    int i;
    l = (artNodeLinear *)n;
    for (i = 0; i < _LINEAR; i++) {
      if (l->map[i] == b) {
        l->radix[i] = (word_t)c;
        break;
      }
    }
  } break;
  case _LINEAR16: {
    int i;
    l16 = (artNodeLinear16 *)n;
    for (i = 0; i < _LINEAR16; i++) {
      if (l16->map[i] == b) {
        l16->radix[i] = (word_t)c;
        break;
      }
    }
  } break;
  case _SPAN:
    s = (artNodeSpan *)n;
    if (s->map[b] != _SPAN)
      s->radix[s->map[b]] = (word_t)c;
  break;
  case _RADIX:
    r = (artNodeRadix *)n;
    if (r->radix[b])
      r->radix[b] = (word_t)c;
  break;
  default: break;
  }
}

void artNodeAddChild (artNode** n, artNode* c, byte_t b) {
  artNodeSingle* p;
  artNodeLinear* l;
  artNodeLinear16* l16;
  artNodeSpan* s;
  artNodeRadix* r;
  byte_t type;

  type = (*n)->head.type;

  if ((type != _RADIX && type == (*n)->head.rcnt)
    || (type == _INNER && (*n)->head.rcnt == type - 1)) {
    artNodeResize(n, 1);
    type = (*n)->head.type;
  }

  switch (type) {
  case _SINGLE:
    p = (artNodeSingle *)*n;
    p->map = b;
    p->radix = (word_t)c;
    p->head.rcnt = 1;
  break;
  case _INNER:
  case _LINEAR: {
    int i;
    l = (artNodeLinear *)*n;
    for (i = 0; i < _LINEAR; i++) {
      if (!l->radix[i]) break;
    }
    l->map[i] = b;
    l->radix[i] = (word_t)c;
    l->head.rcnt += 1;
  } break;
  case _LINEAR16: {
    int i;
    l16 = (artNodeLinear16 *)*n;
    for (i = 0; i < _LINEAR16; i++) {
      if (!l16->radix[i]) break;
    }
    l16->map[i] = b;
    l16->radix[i] = (word_t)c;
    l16->head.rcnt += 1;
  } break;
  case _SPAN:
    s = (artNodeSpan *)*n;
    if (s->map[b] == _SPAN) {
      s->map[b] = s->head.rcnt;
      s->radix[s->head.rcnt] = (word_t)c;
      s->head.rcnt += 1;
    } else {
      s->radix[s->map[b]] = (word_t)c;
    }
  break;
  case _RADIX:
    r = (artNodeRadix *)*n;
    if (!r->radix[b]) {
      r->head.rcnt += 1;
    }
    r->radix[b] = (word_t)c;
  break;
  default: break;
  }
}

void artNodeSetVal (artNode** n, word_t v) {
  artNodeSingle* p;
  artNodeLinear* l;
  artNodeInner* in;
  artNodeLinear16* l16;
  artNodeSpan* s;
  artNodeRadix* r;
  artNodeLeaf* k;
  int i;

  switch ((*n)->head.type) {
    case _LEAF:
      k = (artNodeLeaf *)*n;
      k->val = v;
    break;
    case _SINGLE:
      p = (artNodeSingle *)*n;
      p->val = v;
    break;
    case _INNER:
      if (!v) break;
      in = (artNodeInner *)*n;
      l = artNodeAlloc(_LINEAR);
      for (i = 0; i < _LINEAR; i++) {
        l->map[i] = in->map[i];
        l->radix[i] = in->radix[i];
      }
      l->head.rcnt = in->head.rcnt;
      artNodeCopyPrefix((artNode *)l, *n);
      l->head.type = _LINEAR;
      l->val = v;
      artNodeFree(*n);
      *n = (artNode *)l;
    break;
    case _LINEAR:
      l = (artNodeLinear *)*n;
      if (!v) {
        in = artNodeAlloc(_INNER);
        for (i = 0; i < _LINEAR; i++) {
          in->map[i] = l->map[i];
          in->radix[i] = l->radix[i];
        }
        in->head.rcnt = l->head.rcnt;
        artNodeCopyPrefix((artNode *)in, *n);
        in->head.type = _INNER;
        artNodeFree(*n);
        *n = (artNode *)in;
      } else {
        l->val = v;
      }
    break;
    case _LINEAR16:
      l16 = (artNodeLinear16 *)*n;
      l16->val = v;
    break;
    case _SPAN:
      s = (artNodeSpan *)*n;
      s->val = v;
    break;
    case _RADIX:
      r = (artNodeRadix *)*n;
      r->val = v;
    break;
    default:  break;
  }
}

void artPut (Art* art, byte_t* k, int l, word_t v) {
  artNode *d, *p, *tmp, *n0, *n1;
  int rt, i, idx, pfx = 0;
  byte_t pchar, s0, s1;

  d = p = art->root;

  if (!d || l > 255)
    return;

  for (i = 0; i < l; ) {
    pfx = artNodeCheckPrefix(d, k, l, i);
    if (pfx != d->head.plen) {
      rt = (p == art->root);
      n0 = artNodeAlloc(_SINGLE);
      artNodeSetPrefix(n0, artNodeGetPrefix(d), pfx);
      s0 = artNodePrefixIdx(d, pfx);
      artNodeMovePrefix(d, pfx);
      artNodeAddChild(&n0, d, s0);
      s1 = k[i + pfx];
      if (pfx < l - i) {
        n1 = artNodeAlloc(_LEAF);
        artNodeSetPrefix(n1, k, l);
        artNodeMovePrefix(n1, i + pfx);
        artNodeAddChild(&n0, n1, s1);
        artNodeSetVal(&n1, v);
      } else artNodeSetVal(&n0, v);
      artNodeReplaceChild(p, n0, pchar);
      if (rt) art->root = p;
      break;
    }
    i += pfx;
    tmp = artNodeGetChild(d, k[i]);
    if (tmp) {
      pchar = k[i];
      p = d;
      d = tmp;
      continue;
    }
    if (i == l) {
      artNodeSetVal(&d, v);
      artNodeReplaceChild(p, d, pchar);
      break;
    }
    n0 = artNodeAlloc(_LEAF);
    artNodeSetVal(&n0, v);
    artNodeSetPrefix(n0, k + i, l - i);
    tmp = d;
    rt = (d == art->root);
    artNodeAddChild(&d, n0, k[i]);
    if (rt) {
      art->root = d;
    } else if (tmp != d) {
      artNodeReplaceChild(p, d, pchar);
    }
    break;
  }
  return;
}

word_t artNodeGetVal (artNode* n) {
  word_t v;
  switch (n->head.type) {
    case _LEAF:
      v = ((artNodeLeaf *)n)->val;
    break;
    case _SINGLE:
      v = ((artNodeSingle *)n)->val;
    break;
    case _LINEAR:
      v = ((artNodeLinear *)n)->val;
    break;
    case _LINEAR16:
      v = ((artNodeLinear16 *)n)->val;
    break;
    case _SPAN:
      v = ((artNodeSpan *)n)->val;
    break;
    case _RADIX:
      v = ((artNodeRadix *)n)->val;
    break;
    default:  
      v = 0;
    break;
  }
  return v;
}

word_t artGet (Art* art, byte_t* k, int l) {
  artNode *d;
  word_t v;

  d = artGetNode(art, k, l, 0);
  v = artNodeGetVal(d);
  return v;
}

Art* artNew (void) {
  Art* d = artMalloc(sizeof(Art));
  d->root = artNodeAlloc(_SINGLE);
  return d;
}

int artRemove (Art* art, byte_t* k, int l) {
  artNode *d, *m, *p, *tmp;
  int i = 0, idx = 0, sptr = 0, pfx = 0;
  word_t* stack = artMalloc(sizeof(word_t) * (l + 1));
  byte_t* schars = artMalloc(l + 1), c;

  d = art->root;

  if (!d || l > 255)
    return 0;

  for (i = 0; i < l; ) {
    pfx = artNodeCheckPrefix(d, k, l, i);
    if (pfx != d->head.plen) return 0;
    i += pfx;
    stack[sptr] = (word_t)d;
    schars[sptr] = k[i];
    tmp = artNodeGetChild(d, k[i]);
    if (tmp && i < l) {
      d = tmp;
      sptr++;
      continue;
    }
    break;
  }

  if (!artNodeGetVal(d)) 
    return 0;

  /* custom destroy node value function */

  tmp = d;
  artNodeSetVal(&d, (word_t)NULL);

  if (tmp != d) {
    stack[sptr] = (word_t)d;
    artNodeReplaceChild((artNode *)stack[sptr-1], d, schars[sptr - 1]);
  }

  if (d->head.type != _LEAF) {
    return 1;
  }

  stack[sptr] = (word_t)d;

  for (i = sptr; i > 0; i--) {
    d = (artNode *)stack[i];
    p = (artNode *)stack[i - 1];
    c = schars[i - 1];
    artNodeReplaceChild(p, d, c);
    if (!d->head.rcnt && !artNodeGetVal(d)) {
      artNodeFree(d);
      artNodeRemoveChild(&p, c);
      stack[i] = 0;
    } else if (d->head.rcnt == 1 && !artNodeGetVal(d)) {
      artNodeMergeWithChild(&d);
      artNodeReplaceChild(p, d, c);
      stack[i] = (word_t)d;
    } else {
      break;
    }
    stack[i - 1] = (word_t)p;
  }
  
  art->root = (artNode *)stack[0];
  return 1;
}

artNode* artGetNode (Art* art, byte_t* k, int l, int p) {
  artNode *d, *tmp;
  int i, pfx = 0;
  word_t v;

  d = art->root;

  if (!d || l > 255)
    return NULL;

  for (i = 0; i < l; ) {
    pfx = artNodeCheckPrefix(d, k, l, i);
    if (pfx != d->head.plen) {
      if (!p || !pfx || pfx != l) return NULL;
      else return d;
    }
    i += pfx;
    tmp = artNodeGetChild(d, k[i]);
    if (tmp && i < l) {
      d = tmp;
      continue;
    }
    if (p) {
      if (i == l) return d;
      else return NULL;
    }
    break;
  }
  return d;
}

void __artGetWithPrefix (artNode* n, artVal* v, byte_t* pref, int plen) {
  artNodeSingle* p;
  artNodeLinear* l;
  artNodeLinear16* l16;
  artNodeSpan* s;
  artNodeRadix* r;
  artVal* iter;
  int ln, i, nplen;
  byte_t type;
  word_t val;
  byte_t* npref;

  if (!n) return;
  if (n->head.plen || plen) {
    npref = malloc(n->head.plen + plen);
    memcpy(npref, pref, plen);
    memcpy(npref + plen, artNodeGetPrefix(n), n->head.plen);
  }
  nplen = n->head.plen + plen;
  val = artNodeGetVal(n);
  if (val) {
    iter = v;
    while (iter->next)
      iter = (artVal *)iter->next;
    iter->next = artMalloc(sizeof(artVal));
    iter = (artVal *)iter->next;
    iter->next = NULL;
    iter->val = val;
    iter->key = npref;
  }

  ln = n->head.plen;
  type = n->head.type;

  switch (type) {
  case _SINGLE:
    p = (artNodeSingle *)n;
    __artGetWithPrefix((artNode *)p->radix, v, npref, nplen);
  break;
  case _INNER:
  case _LINEAR:
    l = (artNodeLinear *)n;
    for (i = 0; i < _LINEAR; i++)
      __artGetWithPrefix((artNode *)l->radix[i], v, npref, nplen);
  break;
  case _LINEAR16:
    l16 = (artNodeLinear16 *)n;
    for (i = 0; i < _LINEAR16; i++)
      __artGetWithPrefix((artNode *)l16->radix[i], v, npref, nplen);
  break;
  case _SPAN:
    s = (artNodeSpan *)n;
    for (i = 0; i < _SPAN; i++)
      __artGetWithPrefix((artNode *)s->radix[i], v, npref, nplen);
  break;
  case _RADIX:
    r = (artNodeRadix *)n;
    for (i = 0; i < 256; i++)
      __artGetWithPrefix((artNode *)r->radix[i], v, npref, nplen);
  break;
  default: return;
  }
  return;
}

artVal* artGetWithPrefix (Art* art, byte_t* p, int l) {
  artNode* d;
  artVal* v, *del;
  v = artMalloc(sizeof(artVal));
  v->val = (word_t)0;
  v->next = NULL;
  d = artGetNode(art, p, l, 1);
  __artGetWithPrefix(d, v, p, l - d->head.plen);
  if (v->next) {
    del = v;
    v = v->next;
    free(del);
  } else {
    free(v);
    v = NULL;
  }
  return v;
}

/* testing */
void artNodePrintDetails (artNode* n) {
  int i, ln;
  artNodeSingle* p;
  artNodeLinear* l;
  artNodeLinear16* l16;
  artNodeSpan* s;
  artNodeRadix* r;
  artNodeLeaf* k;
  byte_t type, *pfx;

  printf("----------------------------\n");
  printf("Address: 0x%lx\n", (word_t)n);
  printf("Prefix length: %d\n", (int)n->head.plen);
  printf("Type: %d\n", (int)n->head.type);
  printf("Prefix: \"");
  ln = n->head.plen;
  pfx = artNodeGetPrefix(n);
  for (i = 0; i < ln; i++)
    printf("%c", pfx[i]);
  printf("\"\n");

  type = n->head.type;
  switch (type) {
    case _LEAF:
      k = (artNodeLeaf *)n;
      printf("Value: \"%s\"\n", (char *)k->val);
    break;
    case _SINGLE:
      p = (artNodeSingle *)n;
      printf("Value: \"%s\"\n", (char *)p->val);
      printf("Children: | ");
      printf("0x%lx | ", p->radix);
    break;
    case _INNER:
    case _LINEAR:
      l = (artNodeLinear *)n;
      if (type != _INNER)
        printf("Value: \"%s\"\n", (char *)l->val);
      printf("Children: | ");
      for (i = 0; i < _LINEAR; i++)
        printf("0x%lx | ", l->radix[i]);
    break;
    case _LINEAR16:
      l16 = (artNodeLinear16 *)n;
      printf("Value: \"%s\"\n", (char *)l16->val);
      printf("Map: | ");
      for (i = 0; i < _LINEAR16; i++)
        printf("%c | ", l16->map[i]);
      printf("\n");
      printf("Children: | ");
      for (i = 0; i < _LINEAR16; i++)
        printf("0x%lx | ", l16->radix[i]);
    break;
    case _SPAN:
      s = (artNodeSpan *)n;
      printf("Value: \"%s\"\n", (char *)s->val);
      printf("Map: | ");
      for (i = 0; i < 256; i++)
        if (s->map[i] != 48) printf("%c : %d | ", i, s->map[i]);
      printf("\n");
      printf("Children: | ");
      for (i = 0; i < 48; i++)
        printf("0x%lx | ", s->radix[i]);
    break;
    case _RADIX:
      r = (artNodeRadix *)n;
      printf("Value: \"%s\"\n", (char *)r->val);
      printf("Children: | ");
      for (i = 0; i < 256; i++)
        printf("0x%lx | ", r->radix[i]);
    break;
    default: break;
  }
  printf("\n");
  printf("\n");
  printf("----------------------------\n");
}
