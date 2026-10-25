/* Forgotten first-person replacements used to remain in the level's allocation
 * and GPU-mesh registries. Rocket launchers have both loaded and spent geometry,
 * so repeated switches/respawns retained substantial copies of both. */
static void beanGunRetire(struct beanGunGroup *group)
{
	if (group && !group->retired) {
		group->retiredFrame = s_frameCount;
		group->retired = 1;
	}
}

static void beanGunUnref(struct beanGunGroup *group)
{
	if (group && --group->references == 0) beanGunRetire(group);
}

static void beanGunCollect(void)
{
	struct beanGunGroup **link = &s_gunGroups;
	while (*link) {
		struct beanGunGroup *group = *link;
		if (!group->retired || s_frameCount - group->retiredFrame < 3) {
			link = &group->next;
			continue;
		}
		/* Mesh runs point into the emitted lists. Release the backend's GPU
		 * handles and mesh copies before freeing those lists and vertices. */
		int kept = 0;
		for (int i = 0; i < s_nrigidMeshes; i++) {
			if (s_rigidGroups[i] == group) gevrStaticMeshFree(s_rigidMeshes[i]);
			else {
				s_rigidMeshes[kept] = s_rigidMeshes[i];
				s_rigidGroups[kept++] = s_rigidGroups[i];
			}
		}
		s_nrigidMeshes = kept;
		kept = 0;
		for (int i = 0; i < s_nalloc; i++) {
			if (s_allocGroups[i] == group) {
				/* registerPrimary matches a later list by address against these */
				if (s_alloc[i] == (void *)s_lastEmit) s_lastEmit = NULL;
				if (s_alloc[i] == s_lastEmitVtx) s_lastEmitVtx = NULL;
				free(s_alloc[i]);
			} else {
				s_alloc[kept] = s_alloc[i];
				s_allocGroups[kept++] = s_allocGroups[i];
			}
		}
		s_nalloc = kept;
		*link = group->next;
		free(group);
	}
}

/* Level reset has already freed every registered mesh and allocation. */
static void beanGunReset(void)
{
	while (s_gunGroups) {
		struct beanGunGroup *next = s_gunGroups->next;
		free(s_gunGroups);
		s_gunGroups = next;
	}
	s_registrationGroup = NULL;
	s_lastEmit = NULL;
	s_lastEmitVtx = NULL;
}
